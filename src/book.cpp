#include "lob/book.h"
#include <algorithm>

namespace lob {

Book::~Book() {
    // All orders cleaned up automatically by unique_ptr destructors
}

RestingOrder* Book::allocate_order() {
    // Pre-allocated pool: reuse or allocate new
    if (pool_index_ < order_pool_.size()) {
        RestingOrder* order = order_pool_[pool_index_].get();
        ++pool_index_;
        return order;
    }
    // Grow pool if needed
    order_pool_.push_back(std::make_unique<RestingOrder>());
    ++pool_index_;
    return order_pool_.back().get();
}

void Book::deallocate_order(RestingOrder* order) {
    // Pool-backed allocation: just reset and move back
    // Mark as unused by clearing id
    order->id = 0;
    order->next = nullptr;
    order->prev = nullptr;
}

PriceLevel* Book::get_or_create_level(Price p, Side side) {
    auto& levels = (side == Side::Buy) ? bid_levels_ : ask_levels_;
    auto it = levels.find(p);
    if (it != levels.end()) {
        return it->second.get();
    }
    // Create new level
    auto level = std::make_unique<PriceLevel>();
    level->price = p;
    PriceLevel* ptr = level.get();
    levels[p] = std::move(level);
    return ptr;
}

PriceLevel* Book::find_level(Price p, Side side) const {
    const auto& levels = (side == Side::Buy) ? bid_levels_ : ask_levels_;
    auto it = levels.find(p);
    return (it != levels.end()) ? it->second.get() : nullptr;
}

void Book::prune_empty_level(Price p, Side side) {
    auto& levels = (side == Side::Buy) ? bid_levels_ : ask_levels_;
    auto it = levels.find(p);
    if (it != levels.end() && it->second->order_count == 0) {
        levels.erase(it);
    }
}

std::optional<Price> Book::best_bid_price() const {
    if (bid_levels_.empty()) return std::nullopt;
    Price best = 0;
    for (const auto& [p, _] : bid_levels_) {
        if (best == 0 || p > best) best = p;
    }
    return best;
}

std::optional<Price> Book::best_ask_price() const {
    if (ask_levels_.empty()) return std::nullopt;
    Price best = 0;
    for (const auto& [p, _] : ask_levels_) {
        if (best == 0 || p < best) best = p;
    }
    return best;
}

void Book::submit(const Order& order, EventSink& sink) {
    // validate
    if (order.type != OrderType::Market && order.price <= 0) {
        sink.on_event(Reject{order.id, "invalid price"});
        return;
    }
    if (order.qty <= 0) {
        sink.on_event(Reject{order.id, "invalid quantity"});
        return;
    }
    if (order_index_.count(order.id)) {
        sink.on_event(Reject{order.id, "duplicate order id"});
        return;
    }

    // FOK: pre-check that full qty is available before touching anything
    if (order.type == OrderType::FOK && !can_fill(order)) {
        sink.on_event(Reject{order.id, "FOK not fillable"});
        return;
    }

    sink.on_event(Ack{order.id});

    Qty remaining = match_order(order, sink);

    // STP killed the aggressor (cancel_newest or cancel_both)
    if (remaining == -1) {
        sink.on_event(CancelAck{order.id});
        return;
    }

    if (remaining <= 0) {
        sink.on_event(Filled{order.id});
        return;
    }

    switch (order.type) {
        case OrderType::Limit:
            place_on_book(order, remaining);
            if (remaining < order.qty)
                sink.on_event(Partial{order.id, remaining});
            break;

        case OrderType::Market:
        case OrderType::IOC:
            // never rests — kill whatever's left
            if (remaining < order.qty)
                sink.on_event(Partial{order.id, remaining});
            sink.on_event(CancelAck{order.id});
            break;

        case OrderType::FOK:
            // shouldn't happen (we checked can_fill), but just in case
            sink.on_event(Reject{order.id, "FOK partial fill (bug)"});
            break;
    }
}

void Book::cancel(OrderId id, EventSink& sink) {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) {
        sink.on_event(Reject{id, "order not found"});
        return;
    }

    RestingOrder* order = it->second;
    remove_order(order, order->side);
    sink.on_event(CancelAck{id});
}

void Book::modify(OrderId id, Price new_price, Qty new_qty, EventSink& sink) {
    auto it = order_index_.find(id);
    if (it == order_index_.end()) {
        sink.on_event(Reject{id, "order not found"});
        return;
    }

    if (new_qty <= 0) {
        sink.on_event(Reject{id, "invalid quantity"});
        return;
    }
    if (new_price <= 0) {
        sink.on_event(Reject{id, "invalid price"});
        return;
    }

    RestingOrder* order = it->second;
    Side side = order->side;
    TraderId trader_id = order->trader_id;
    Timestamp new_ts = order->ts + 1;

    // remove old resting order (no events — this is not a cancel)
    remove_order(order, side);

    // re-insert at new price/qty — loses time priority, placed at back of queue
    order->price = new_price;
    order->remaining = new_qty;
    order->ts = new_ts;

    PriceLevel* level = get_or_create_level(new_price, side);
    level->add_order(order);

    sink.on_event(ModifyAck{id, new_price, new_qty});
}

MarketDepth Book::depth(int levels) const {
    MarketDepth md;
    if (levels <= 0) return md;

    // Collect and sort bid prices
    std::vector<Price> bid_prices;
    for (const auto& [p, _] : bid_levels_) {
        bid_prices.push_back(p);
    }
    std::sort(bid_prices.rbegin(), bid_prices.rend()); // high to low

    md.bids.reserve(std::min<size_t>(levels, bid_prices.size()));
    for (Price p : bid_prices) {
        if (static_cast<int>(md.bids.size()) >= levels) break;
        PriceLevel* level = find_level(p, Side::Buy);
        if (!level) continue;
        
        Qty total = 0;
        for (RestingOrder* o = level->head; o != nullptr; o = o->next) {
            total += o->remaining;
        }
        md.bids.push_back(DepthLevel{p, total, level->order_count});
    }

    // Collect and sort ask prices
    std::vector<Price> ask_prices;
    for (const auto& [p, _] : ask_levels_) {
        ask_prices.push_back(p);
    }
    std::sort(ask_prices.begin(), ask_prices.end()); // low to high

    md.asks.reserve(std::min<size_t>(levels, ask_prices.size()));
    for (Price p : ask_prices) {
        if (static_cast<int>(md.asks.size()) >= levels) break;
        PriceLevel* level = find_level(p, Side::Sell);
        if (!level) continue;
        
        Qty total = 0;
        for (RestingOrder* o = level->head; o != nullptr; o = o->next) {
            total += o->remaining;
        }
        md.asks.push_back(DepthLevel{p, total, level->order_count});
    }

    return md;
}

TopOfBook Book::top() const {
    TopOfBook tob;

    auto best_bid = best_bid_price();
    if (best_bid) {
        PriceLevel* level = find_level(*best_bid, Side::Buy);
        if (level) {
            tob.best_bid = *best_bid;
            for (RestingOrder* o = level->head; o != nullptr; o = o->next) {
                tob.bid_qty += o->remaining;
            }
        }
    }

    auto best_ask = best_ask_price();
    if (best_ask) {
        PriceLevel* level = find_level(*best_ask, Side::Sell);
        if (level) {
            tob.best_ask = *best_ask;
            for (RestingOrder* o = level->head; o != nullptr; o = o->next) {
                tob.ask_qty += o->remaining;
            }
        }
    }

    return tob;
}

Qty Book::match_order(const Order& incoming, EventSink& sink) {
    Qty remaining = incoming.qty;
    bool is_market = (incoming.type == OrderType::Market);
    bool stp_active = (stp_mode_ != STPMode::None && incoming.trader_id != 0);

    auto& opp_levels = (incoming.side == Side::Buy) ? ask_levels_ : bid_levels_;
    
    // Collect prices and sort for iteration
    std::vector<Price> prices;
    for (const auto& [p, _] : opp_levels) {
        prices.push_back(p);
    }
    
    if (incoming.side == Side::Buy) {
        std::sort(prices.begin(), prices.end()); // low to high (ascending asks)
    } else {
        std::sort(prices.rbegin(), prices.rend()); // high to low (descending bids)
    }

    for (Price lvl_price : prices) {
        if (remaining <= 0) break;

        if (!is_market) {
            if (incoming.side == Side::Buy && lvl_price > incoming.price) break;
            if (incoming.side == Side::Sell && lvl_price < incoming.price) break;
        }

        PriceLevel* level = (incoming.side == Side::Buy) 
            ? find_level(lvl_price, Side::Sell)
            : find_level(lvl_price, Side::Buy);
        
        if (!level) continue;

        RestingOrder* it = level->head;
        while (remaining > 0 && it != nullptr) {
            RestingOrder* next = it->next;

            // self-trade check
            if (stp_active && it->trader_id == incoming.trader_id && it->trader_id != 0) {
                OrderId rid = it->id;

                switch (stp_mode_) {
                    case STPMode::CancelNewest:
                        sink.on_event(STPCancel{incoming.id, rid, stp_mode_});
                        remaining = -1;
                        return remaining;

                    case STPMode::CancelOldest:
                        sink.on_event(STPCancel{incoming.id, rid, stp_mode_});
                        remove_order(it, it->side);
                        it = next;
                        continue;

                    case STPMode::CancelBoth:
                        sink.on_event(STPCancel{incoming.id, rid, stp_mode_});
                        remove_order(it, it->side);
                        remaining = -1;
                        return remaining;

                    case STPMode::None:
                        break;
                }
            }

            Qty fill = std::min(remaining, it->remaining);
            sink.on_event(Trade{incoming.id, it->id, it->price, fill, incoming.ts});
            remaining -= fill;
            it->remaining -= fill;

            if (it->remaining <= 0) {
                OrderId rid = it->id;
                remove_order(it, it->side);
                sink.on_event(Filled{rid});
            }

            it = next;
        }

        prune_empty_level(lvl_price, incoming.side == Side::Buy ? Side::Sell : Side::Buy);
    }

    return remaining;
}

bool Book::can_fill(const Order& order) const {
    Qty need = order.qty;
    bool stp_active = (stp_mode_ != STPMode::None && order.trader_id != 0);

    if (order.side == Side::Buy) {
        // Collect ask prices and sort
        std::vector<Price> prices;
        for (const auto& [p, _] : ask_levels_) {
            if (p > order.price) break;
            prices.push_back(p);
        }
        std::sort(prices.begin(), prices.end());

        for (Price p : prices) {
            if (need <= 0) return true;
            PriceLevel* level = find_level(p, Side::Sell);
            if (!level) continue;

            for (RestingOrder* o = level->head; o != nullptr; o = o->next) {
                if (stp_active && o->trader_id == order.trader_id) {
                    if (stp_mode_ == STPMode::CancelOldest) continue;
                    return false;
                }
                need -= o->remaining;
                if (need <= 0) return true;
            }
        }
    } else {
        // Collect bid prices and sort
        std::vector<Price> prices;
        for (const auto& [p, _] : bid_levels_) {
            if (p < order.price) break;
            prices.push_back(p);
        }
        std::sort(prices.rbegin(), prices.rend());

        for (Price p : prices) {
            if (need <= 0) return true;
            PriceLevel* level = find_level(p, Side::Buy);
            if (!level) continue;

            for (RestingOrder* o = level->head; o != nullptr; o = o->next) {
                if (stp_active && o->trader_id == order.trader_id) {
                    if (stp_mode_ == STPMode::CancelOldest) continue;
                    return false;
                }
                need -= o->remaining;
                if (need <= 0) return true;
            }
        }
    }

    return need <= 0;
}

void Book::place_on_book(const Order& order, Qty remaining) {
    RestingOrder* ro = allocate_order();
    ro->id = order.id;
    ro->trader_id = order.trader_id;
    ro->side = order.side;
    ro->price = order.price;
    ro->remaining = remaining;
    ro->ts = order.ts;

    PriceLevel* level = get_or_create_level(order.price, order.side);
    level->add_order(ro);
    order_index_[order.id] = ro;
}

void Book::remove_order(RestingOrder* order, Side side) {
    order_index_.erase(order->id);
    
    PriceLevel* level = find_level(order->price, side);
    if (level) {
        level->remove_order(order);
        prune_empty_level(order->price, side);
    }
    
    deallocate_order(order);
}

std::vector<RestingOrder> Book::dump_orders() const {
    std::vector<RestingOrder> out;
    out.reserve(order_index_.size());

    // bids first (high to low)
    std::vector<Price> bid_prices;
    for (const auto& [p, _] : bid_levels_) {
        bid_prices.push_back(p);
    }
    std::sort(bid_prices.rbegin(), bid_prices.rend());

    for (Price p : bid_prices) {
        PriceLevel* level = find_level(p, Side::Buy);
        if (level) {
            for (RestingOrder* o = level->head; o != nullptr; o = o->next) {
                out.push_back(*o);
            }
        }
    }

    // then asks (low to high)
    std::vector<Price> ask_prices;
    for (const auto& [p, _] : ask_levels_) {
        ask_prices.push_back(p);
    }
    std::sort(ask_prices.begin(), ask_prices.end());

    for (Price p : ask_prices) {
        PriceLevel* level = find_level(p, Side::Sell);
        if (level) {
            for (RestingOrder* o = level->head; o != nullptr; o = o->next) {
                out.push_back(*o);
            }
        }
    }

    return out;
}

void Book::restore_order(const RestingOrder& ro) {
    RestingOrder* order = allocate_order();
    *order = ro;

    PriceLevel* level = get_or_create_level(ro.price, ro.side);
    level->add_order(order);
    order_index_[ro.id] = order;
}

} // namespace lob
