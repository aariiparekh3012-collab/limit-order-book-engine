#pragma once

#include "types.h"
#include "event.h"

#include <map>
#include <list>
#include <unordered_map>
#include <functional>
#include <optional>

namespace lob {

struct TopOfBook {
    std::optional<Price> best_bid;
    std::optional<Price> best_ask;
    Qty bid_qty = 0;
    Qty ask_qty = 0;
};

class Book {
public:
    void submit(const Order& order, EventSink& sink);
    void cancel(OrderId id, EventSink& sink);
    TopOfBook top() const;

    size_t order_count() const { return order_index_.size(); }

private:
    struct RestingOrder {
        OrderId   id;
        Side      side;
        Price     price;
        Qty       remaining;
        Timestamp ts;
    };

    using Queue    = std::list<RestingOrder>;
    using Iterator = Queue::iterator;

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
