#pragma once

#include "types.h"
#include "event.h"

#include <map>
#include <list>
#include <unordered_map>
#include <functional>
#include <optional>
#include <vector>

namespace lob {

struct TopOfBook {
    std::optional<Price> best_bid;
    std::optional<Price> best_ask;
    Qty bid_qty = 0;
    Qty ask_qty = 0;
};

// public so snapshot code can read/write it
struct RestingOrder {
    OrderId   id;
    TraderId  trader_id;
    Side      side;
    Price     price;
    Qty       remaining;
    Timestamp ts;
};

struct DepthLevel {
    Price price;
    Qty   qty;
    int   order_count;
};

struct MarketDepth {
    std::vector<DepthLevel> bids;  // best (highest) first
    std::vector<DepthLevel> asks;  // best (lowest) first
};

class Book {
public:
    explicit Book(STPMode stp = STPMode::None) : stp_mode_(stp) {}

    void submit(const Order& order, EventSink& sink);
    void cancel(OrderId id, EventSink& sink);
    void modify(OrderId id, Price new_price, Qty new_qty, EventSink& sink);
    TopOfBook top() const;
    MarketDepth depth(int levels) const;

    size_t order_count() const { return order_index_.size(); }
    STPMode stp_mode() const { return stp_mode_; }

    // snapshot support: dump all resting orders in price-time priority
    std::vector<RestingOrder> dump_orders() const;

    // snapshot support: insert an order directly (no matching, no events)
    // used to rebuild from a binary snapshot
    void restore_order(const RestingOrder& ro);

private:
    using Queue    = std::list<RestingOrder>;
    using Iterator = Queue::iterator;

    STPMode stp_mode_;

    // bids sorted high-to-low, asks sorted low-to-high
    std::map<Price, Queue, std::greater<Price>> bids_;
    std::map<Price, Queue>                       asks_;

    // O(1) cancel index: order_id -> iterator into its queue
    std::unordered_map<OrderId, Iterator> order_index_;

    Qty  match_order(const Order& incoming, EventSink& sink);
    bool can_fill(const Order& order) const;
    void place_on_book(const Order& order, Qty remaining);
    void remove_order(Iterator it, Side side);
};

} // namespace lob
