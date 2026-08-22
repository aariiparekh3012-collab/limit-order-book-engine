#include "lob/book.h"

namespace lob {

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

    Iterator order_it = it->second;
    remove_order(order_it, order_it->side);
    sink.on_event(CancelAck{id});
}

TopOfBook Book::top() const {
    TopOfBook tob;

    if (!bids_.empty()) {
        auto& [price, queue] = *bids_.begin();
        tob.best_bid = price;
        for (const auto& o : queue) tob.bid_qty += o.remaining;
    }
    if (!asks_.empty()) {
        auto& [price, queue] = *asks_.begin();
        tob.best_ask = price;
        for (const auto& o : queue) tob.ask_qty += o.remaining;
    }

    return tob;
}

// walks opposite side while prices cross, emits trades
Qty Book::match_order(const Order& incoming, EventSink& sink) {
    Qty remaining = incoming.qty;
    bool is_market = (incoming.type == OrderType::Market);

    auto do_match = [&](auto& side_map, auto price_ok) {
        while (remaining > 0 && !side_map.empty()) {
            auto lvl = side_map.begin();
            if (!is_market && !price_ok(lvl->first)) break;

            auto& queue = lvl->second;
            while (remaining > 0 && !queue.empty()) {
                auto& resting = queue.front();
                Qty fill = std::min(remaining, resting.remaining);

                sink.on_event(Trade{incoming.id, resting.id, resting.price, fill, incoming.ts});
                remaining -= fill;
                resting.remaining -= fill;

                if (resting.remaining <= 0) {
                    OrderId rid = resting.id;
                    order_index_.erase(rid);
                    queue.pop_front();
                    sink.on_event(Filled{rid});
                }
            }
            if (queue.empty()) side_map.erase(lvl);
        }
    };

    if (incoming.side == Side::Buy)
        do_match(asks_, [&](Price p) { return p <= incoming.price; });
    else
        do_match(bids_, [&](Price p) { return p >= incoming.price; });

    return remaining;
}

// read-only check for FOK: can we fill the full qty?
bool Book::can_fill(const Order& order) const {
    Qty need = order.qty;

    if (order.side == Side::Buy) {
        for (auto it = asks_.begin(); it != asks_.end() && need > 0; ++it) {
            if (it->first > order.price) break;
            for (const auto& r : it->second) {
                need -= r.remaining;
                if (need <= 0) return true;
            }
        }
    } else {
        for (auto it = bids_.begin(); it != bids_.end() && need > 0; ++it) {
            if (it->first < order.price) break;
            for (const auto& r : it->second) {
                need -= r.remaining;
                if (need <= 0) return true;
            }
        }
    }
    return need <= 0;
}

void Book::place_on_book(const Order& order, Qty remaining) {
    RestingOrder ro{order.id, order.side, order.price, remaining, order.ts};

    if (order.side == Side::Buy) {
        auto& q = bids_[order.price];
        q.push_back(ro);
        order_index_[order.id] = std::prev(q.end());
    } else {
        auto& q = asks_[order.price];
        q.push_back(ro);
        order_index_[order.id] = std::prev(q.end());
    }
}

void Book::remove_order(Iterator it, Side side) {
    Price price = it->price;
    order_index_.erase(it->id);

    if (side == Side::Buy) {
        auto lvl = bids_.find(price);
        if (lvl != bids_.end()) {
            lvl->second.erase(it);
            if (lvl->second.empty()) bids_.erase(lvl);
        }
    } else {
        auto lvl = asks_.find(price);
        if (lvl != asks_.end()) {
            lvl->second.erase(it);
            if (lvl->second.empty()) asks_.erase(lvl);
        }
    }
}

} // namespace lob
