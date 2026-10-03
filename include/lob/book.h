#pragma once

#include "types.h"
#include "event.h"

#include <unordered_map>
#include <vector>
#include <optional>
#include <memory>

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
    
    // Intrusive links for order queue within a price level
    RestingOrder* next = nullptr;
    RestingOrder* prev = nullptr;
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

// Price level bucket: stores orders in FIFO order via intrusive list
struct PriceLevel {
    Price price;
    RestingOrder* head = nullptr;
    RestingOrder* tail = nullptr;
    int order_count = 0;
    
    void add_order(RestingOrder* order) {
        order->next = nullptr;
        order->prev = tail;
        if (tail) tail->next = order;
        else head = order;
        tail = order;
        ++order_count;
    }
    
    void remove_order(RestingOrder* order) {
        if (order->prev) order->prev->next = order->next;
        else head = order->next;
        if (order->next) order->next->prev = order->prev;
        else tail = order->prev;
        --order_count;
    }
};

class Book {
public:
    explicit Book(STPMode stp = STPMode::None) : stp_mode_(stp) {}
    
    ~Book();

    void submit(const Order& order, EventSink& sink);
    void cancel(OrderId id, EventSink& sink);
    void modify(OrderId id, Price new_price, Qty new_qty, EventSink& sink);
    TopOfBook top() const;
    MarketDepth depth(int levels) const;
    bool contains(OrderId id) const { return order_index_.count(id) != 0; }

    size_t order_count() const { return order_index_.size(); }
    STPMode stp_mode() const { return stp_mode_; }

    // snapshot support: dump all resting orders in price-time priority
    std::vector<RestingOrder> dump_orders() const;

    // snapshot support: insert an order directly (no matching, no events)
    // used to rebuild from a binary snapshot
    void restore_order(const RestingOrder& ro);

private:
    STPMode stp_mode_;

    // Sparse price-level map: price -> PriceLevel*
    // Uses intrusive linked lists to avoid per-order allocation
    std::unordered_map<Price, std::unique_ptr<PriceLevel>> bid_levels_;
    std::unordered_map<Price, std::unique_ptr<PriceLevel>> ask_levels_;

    // Fast O(1) order lookup: order_id -> RestingOrder*
    std::unordered_map<OrderId, RestingOrder*> order_index_;

    // Pool of pre-allocated order nodes to reduce allocation churn
    std::vector<std::unique_ptr<RestingOrder>> order_pool_;
    size_t pool_index_ = 0;

    Qty  match_order(const Order& incoming, EventSink& sink);
    bool can_fill(const Order& order) const;
    void place_on_book(const Order& order, Qty remaining);
    void remove_order(RestingOrder* order, Side side);
    
    RestingOrder* allocate_order();
    void deallocate_order(RestingOrder* order);
    
    PriceLevel* get_or_create_level(Price p, Side side);
    PriceLevel* find_level(Price p, Side side) const;
    void prune_empty_level(Price p, Side side);
    
    // Iterate price levels in best-price-first order
    using LevelIterator = std::unordered_map<Price, std::unique_ptr<PriceLevel>>::iterator;
    using ConstLevelIterator = std::unordered_map<Price, std::unique_ptr<PriceLevel>>::const_iterator;
    
    struct BidComparator {
        bool operator()(Price a, Price b) const { return a > b; }
    };
    struct AskComparator {
        bool operator()(Price a, Price b) const { return a < b; }
    };
    
    // Helper to get best price (top of book)
    std::optional<Price> best_bid_price() const;
    std::optional<Price> best_ask_price() const;
};

} // namespace lob
