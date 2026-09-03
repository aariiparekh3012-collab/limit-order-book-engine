#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "lob/book.h"
#include "lob/exchange.h"

using namespace lob;

static uint64_t next_ts = 1;

Order buy(OrderId id, Price price, Qty qty, OrderType type = OrderType::Limit, TraderId tid = 0) {
    return {id, Side::Buy, price, qty, next_ts++, type, "", tid};
}

Order sell(OrderId id, Price price, Qty qty, OrderType type = OrderType::Limit, TraderId tid = 0) {
    return {id, Side::Sell, price, qty, next_ts++, type, "", tid};
}

Order market_buy(OrderId id, Qty qty) {
    return {id, Side::Buy, 0, qty, next_ts++, OrderType::Market};
}

Order market_sell(OrderId id, Qty qty) {
    return {id, Side::Sell, 0, qty, next_ts++, OrderType::Market};
}

template<typename T>
bool has(const VectorSink& s) {
    for (auto& e : s.events) if (std::holds_alternative<T>(e)) return true;
    return false;
}

template<typename T>
const T& first(const VectorSink& s) {
    for (auto& e : s.events) if (std::holds_alternative<T>(e)) return std::get<T>(e);
    throw std::runtime_error("event not found");
}

size_t trade_count(const VectorSink& s) {
    size_t n = 0;
    for (auto& e : s.events) if (std::holds_alternative<Trade>(e)) n++;
    return n;
}

void check_no_crossed_book(const Book& b) {
    auto tob = b.top();
    if (tob.best_bid && tob.best_ask)
        REQUIRE(*tob.best_bid < *tob.best_ask);
}

// --- limit orders ---

TEST_CASE("resting order on empty book", "[limit]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s);
    REQUIRE(has<Ack>(s));
    REQUIRE_FALSE(has<Trade>(s));
    REQUIRE(b.order_count() == 1);
}

TEST_CASE("exact cross", "[limit]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s); s.clear();
    b.submit(sell(2, 100, 50), s);
    auto& t = first<Trade>(s);
    REQUIRE(t.aggressor_id == 2);
    REQUIRE(t.resting_id == 1);
    REQUIRE(t.qty == 50);
    REQUIRE(b.order_count() == 0);
}

TEST_CASE("no cross when sell above bid", "[limit]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s); s.clear();
    b.submit(sell(2, 101, 50), s);
    REQUIRE_FALSE(has<Trade>(s));
    REQUIRE(b.order_count() == 2);
    check_no_crossed_book(b);
}

TEST_CASE("partial fill — aggressor leftover rests", "[limit]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 30), s); s.clear();
    b.submit(sell(2, 100, 80), s);
    REQUIRE(first<Trade>(s).qty == 30);
    REQUIRE(first<Partial>(s).remaining == 50);
    REQUIRE(b.order_count() == 1);
}

TEST_CASE("partial fill — resting partially consumed", "[limit]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 80), s); s.clear();
    b.submit(sell(2, 100, 30), s);
    REQUIRE(first<Trade>(s).qty == 30);
    REQUIRE(has<Filled>(s));
    REQUIRE(b.order_count() == 1);
}

TEST_CASE("FIFO at same price level", "[limit]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 40), s);
    b.submit(buy(2, 100, 40), s); s.clear();
    b.submit(sell(3, 100, 40), s);
    REQUIRE(first<Trade>(s).resting_id == 1);
}

TEST_CASE("price priority", "[limit]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s);
    b.submit(buy(2, 102, 50), s);
    b.submit(buy(3, 101, 50), s); s.clear();
    b.submit(sell(4, 100, 50), s);
    REQUIRE(first<Trade>(s).resting_id == 2);
    REQUIRE(first<Trade>(s).price == 102);
}

TEST_CASE("fills across multiple price levels", "[limit]") {
    Book b; VectorSink s;
    b.submit(buy(1, 102, 30), s);
    b.submit(buy(2, 101, 40), s);
    b.submit(buy(3, 100, 50), s); s.clear();
    b.submit(sell(4, 100, 60), s);
    REQUIRE(trade_count(s) == 2);
    REQUIRE(b.order_count() == 2);
}

TEST_CASE("trade executes at resting price", "[limit]") {
    Book b; VectorSink s;
    b.submit(buy(1, 105, 50), s); s.clear();
    b.submit(sell(2, 100, 50), s);
    REQUIRE(first<Trade>(s).price == 105);
}

// --- market orders ---

TEST_CASE("market buy sweeps asks", "[market]") {
    Book b; VectorSink s;
    b.submit(sell(1, 100, 50), s);
    b.submit(sell(2, 101, 50), s); s.clear();
    b.submit(market_buy(3, 70), s);
    REQUIRE(trade_count(s) == 2);
    REQUIRE(first<Trade>(s).price == 100);
}

TEST_CASE("market order with no liquidity gets cancelled", "[market]") {
    Book b; VectorSink s;
    b.submit(market_buy(1, 50), s);
    REQUIRE(has<Ack>(s));
    REQUIRE(has<CancelAck>(s));
    REQUIRE_FALSE(has<Trade>(s));
}

TEST_CASE("market order never rests", "[market]") {
    Book b; VectorSink s;
    b.submit(sell(1, 100, 30), s); s.clear();
    b.submit(market_buy(2, 50), s);
    REQUIRE(has<Trade>(s));
    REQUIRE(has<CancelAck>(s));
    REQUIRE(b.order_count() == 0);
}

TEST_CASE("market sell sweeps all bid levels", "[market]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 20), s);
    b.submit(buy(2, 90, 30), s);
    b.submit(buy(3, 80, 50), s); s.clear();
    b.submit(market_sell(4, 100), s);
    REQUIRE(trade_count(s) == 3);
    REQUIRE(b.order_count() == 0);
}

// --- IOC ---

TEST_CASE("IOC partial fill then cancel", "[ioc]") {
    Book b; VectorSink s;
    b.submit(sell(1, 100, 30), s); s.clear();
    b.submit(buy(2, 100, 80, OrderType::IOC), s);
    REQUIRE(first<Trade>(s).qty == 30);
    REQUIRE(has<CancelAck>(s));
    REQUIRE(b.order_count() == 0);
}

TEST_CASE("IOC with no match cancelled immediately", "[ioc]") {
    Book b; VectorSink s;
    b.submit(sell(1, 200, 50), s); s.clear();
    b.submit(buy(2, 100, 50, OrderType::IOC), s);
    REQUIRE(has<CancelAck>(s));
    REQUIRE_FALSE(has<Trade>(s));
}

TEST_CASE("IOC fully filled emits Filled", "[ioc]") {
    Book b; VectorSink s;
    b.submit(sell(1, 100, 50), s); s.clear();
    b.submit(buy(2, 100, 50, OrderType::IOC), s);
    REQUIRE(has<Filled>(s));
    REQUIRE_FALSE(has<CancelAck>(s));
}

// --- FOK ---

TEST_CASE("FOK fills when qty available", "[fok]") {
    Book b; VectorSink s;
    b.submit(sell(1, 100, 50), s); s.clear();
    b.submit(buy(2, 100, 50, OrderType::FOK), s);
    REQUIRE(has<Ack>(s));
    REQUIRE(has<Filled>(s));
    REQUIRE(b.order_count() == 0);
}

TEST_CASE("FOK rejected on insufficient qty", "[fok]") {
    Book b; VectorSink s;
    b.submit(sell(1, 100, 30), s); s.clear();
    b.submit(buy(2, 100, 50, OrderType::FOK), s);
    REQUIRE(has<Reject>(s));
    REQUIRE(b.order_count() == 1); // book untouched
}

TEST_CASE("FOK rejected on empty book", "[fok]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50, OrderType::FOK), s);
    REQUIRE(has<Reject>(s));
    REQUIRE_FALSE(has<Ack>(s));
}

TEST_CASE("FOK fills across price levels", "[fok]") {
    Book b; VectorSink s;
    b.submit(sell(1, 100, 30), s);
    b.submit(sell(2, 101, 30), s); s.clear();
    b.submit(buy(3, 101, 55, OrderType::FOK), s);
    REQUIRE(has<Ack>(s));
    REQUIRE(trade_count(s) == 2);
    REQUIRE(has<Filled>(s));
}

// --- cancel ---

TEST_CASE("cancel resting order", "[cancel]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s); s.clear();
    b.cancel(1, s);
    REQUIRE(has<CancelAck>(s));
    REQUIRE(b.order_count() == 0);
}

TEST_CASE("cancel nonexistent", "[cancel]") {
    Book b; VectorSink s;
    b.cancel(999, s);
    REQUIRE(has<Reject>(s));
}

TEST_CASE("cancel middle of queue preserves FIFO", "[cancel]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 10), s);
    b.submit(buy(2, 100, 20), s);
    b.submit(buy(3, 100, 30), s); s.clear();

    b.cancel(2, s);
    REQUIRE(has<CancelAck>(s));

    s.clear();
    b.submit(sell(4, 100, 10), s);
    REQUIRE(first<Trade>(s).resting_id == 1);
}

// --- validation ---

TEST_CASE("reject duplicate id", "[validation]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s); s.clear();
    b.submit(sell(1, 200, 50), s);
    REQUIRE(first<Reject>(s).reason == "duplicate order id");
}

TEST_CASE("reject bad price", "[validation]") {
    Book b; VectorSink s;
    b.submit(buy(1, 0, 50), s);
    REQUIRE(has<Reject>(s));
    s.clear();
    b.submit(buy(2, -10, 50), s);
    REQUIRE(has<Reject>(s));
}

TEST_CASE("reject bad qty", "[validation]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 0), s);
    REQUIRE(has<Reject>(s));
    s.clear();
    b.submit(buy(2, 100, -5), s);
    REQUIRE(has<Reject>(s));
}

TEST_CASE("top of book tracking", "[top]") {
    Book b; VectorSink s;
    REQUIRE_FALSE(b.top().best_bid.has_value());

    b.submit(buy(1, 100, 50), s);
    REQUIRE(b.top().best_bid == 100);

    b.submit(buy(2, 102, 30), s);
    REQUIRE(b.top().best_bid == 102);

    b.submit(sell(3, 105, 20), s);
    REQUIRE(b.top().best_ask == 105);
    check_no_crossed_book(b);
}

// --- exchange (multi-symbol) ---

TEST_CASE("exchange routes to correct book", "[exchange]") {
    Exchange ex; VectorSink s;
    ex.add_symbol("AAPL");
    ex.add_symbol("TSLA");

    ex.submit({1, Side::Buy, 150, 100, 1, OrderType::Limit, "AAPL"}, s);
    ex.submit({2, Side::Buy, 250, 50,  2, OrderType::Limit, "TSLA"}, s);

    REQUIRE(ex.book("AAPL").order_count() == 1);
    REQUIRE(ex.book("TSLA").order_count() == 1);
    REQUIRE(ex.top("AAPL").best_bid == 150);
    REQUIRE(ex.top("TSLA").best_bid == 250);
}

TEST_CASE("exchange rejects unknown symbol", "[exchange]") {
    Exchange ex; VectorSink s;
    ex.add_symbol("AAPL");
    ex.submit({1, Side::Buy, 150, 100, 1, OrderType::Limit, "GOOG"}, s);
    REQUIRE(has<Reject>(s));
}

TEST_CASE("different symbols don't cross", "[exchange]") {
    Exchange ex; VectorSink s;
    ex.add_symbol("AAPL");
    ex.add_symbol("TSLA");

    ex.submit({1, Side::Buy,  150, 100, 1, OrderType::Limit, "AAPL"}, s);
    ex.submit({2, Side::Sell, 150, 100, 2, OrderType::Limit, "TSLA"}, s);
    REQUIRE_FALSE(has<Trade>(s));
    REQUIRE(ex.total_order_count() == 2);
}

TEST_CASE("same symbol crosses through exchange", "[exchange]") {
    Exchange ex; VectorSink s;
    ex.add_symbol("AAPL");

    ex.submit({1, Side::Buy,  150, 100, 1, OrderType::Limit, "AAPL"}, s);
    ex.submit({2, Side::Sell, 150, 100, 2, OrderType::Limit, "AAPL"}, s);
    REQUIRE(has<Trade>(s));
    REQUIRE(ex.total_order_count() == 0);
}

// --- self-trade prevention ---

TEST_CASE("STP cancel_newest kills aggressor", "[stp]") {
    Book b(STPMode::CancelNewest); VectorSink s;
    b.submit(buy(1, 100, 50, OrderType::Limit, 42), s); s.clear();
    b.submit(sell(2, 100, 50, OrderType::Limit, 42), s);  // same trader
    REQUIRE(has<STPCancel>(s));
    REQUIRE(has<CancelAck>(s));           // aggressor cancelled
    REQUIRE_FALSE(has<Trade>(s));         // no trade
    REQUIRE(b.order_count() == 1);        // resting order untouched
}

TEST_CASE("STP cancel_oldest kills resting", "[stp]") {
    Book b(STPMode::CancelOldest); VectorSink s;
    b.submit(buy(1, 100, 50, OrderType::Limit, 42), s); s.clear();
    b.submit(sell(2, 100, 50, OrderType::Limit, 42), s);  // same trader
    REQUIRE(has<STPCancel>(s));
    REQUIRE_FALSE(has<Trade>(s));
    // resting was cancelled, aggressor rests instead
    REQUIRE(b.order_count() == 1);
}

TEST_CASE("STP cancel_both kills both sides", "[stp]") {
    Book b(STPMode::CancelBoth); VectorSink s;
    b.submit(buy(1, 100, 50, OrderType::Limit, 42), s); s.clear();
    b.submit(sell(2, 100, 50, OrderType::Limit, 42), s);  // same trader
    REQUIRE(has<STPCancel>(s));
    REQUIRE(has<CancelAck>(s));
    REQUIRE_FALSE(has<Trade>(s));
    REQUIRE(b.order_count() == 0);        // both gone
}

TEST_CASE("STP does not fire for different traders", "[stp]") {
    Book b(STPMode::CancelNewest); VectorSink s;
    b.submit(buy(1, 100, 50, OrderType::Limit, 42), s); s.clear();
    b.submit(sell(2, 100, 50, OrderType::Limit, 99), s);  // different trader
    REQUIRE(has<Trade>(s));
    REQUIRE_FALSE(has<STPCancel>(s));
    REQUIRE(b.order_count() == 0);
}

TEST_CASE("STP cancel_oldest skips self and fills next", "[stp]") {
    Book b(STPMode::CancelOldest); VectorSink s;
    // two resting bids: one from trader 42, one from trader 99
    b.submit(buy(1, 100, 50, OrderType::Limit, 42), s);
    b.submit(buy(2, 100, 50, OrderType::Limit, 99), s); s.clear();
    // trader 42 sells — should skip their own resting bid, fill against trader 99
    b.submit(sell(3, 100, 50, OrderType::Limit, 42), s);
    REQUIRE(has<STPCancel>(s));    // own resting order cancelled
    REQUIRE(has<Trade>(s));        // traded with trader 99's order
    REQUIRE(first<Trade>(s).resting_id == 2);
}

// --- market depth ---

TEST_CASE("depth returns correct levels", "[depth]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 10), s);
    b.submit(buy(2, 99, 10), s);
    b.submit(buy(3, 98, 10), s);
    b.submit(sell(4, 101, 10), s);
    b.submit(sell(5, 102, 10), s);
    b.submit(sell(6, 103, 10), s);

    auto d5 = b.depth(5);
    REQUIRE(d5.bids.size() == 3);
    REQUIRE(d5.asks.size() == 3);
    REQUIRE(d5.bids[0].price == 100);
    REQUIRE(d5.bids[1].price == 99);
    REQUIRE(d5.bids[2].price == 98);
    REQUIRE(d5.asks[0].price == 101);
    REQUIRE(d5.asks[1].price == 102);
    REQUIRE(d5.asks[2].price == 103);

    auto d2 = b.depth(2);
    REQUIRE(d2.bids.size() == 2);
    REQUIRE(d2.asks.size() == 2);
    REQUIRE(d2.bids[0].price == 100);
    REQUIRE(d2.bids[1].price == 99);
    REQUIRE(d2.asks[0].price == 101);
    REQUIRE(d2.asks[1].price == 102);
}

TEST_CASE("depth aggregates qty within level", "[depth]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 10), s);
    b.submit(buy(2, 100, 20), s);
    b.submit(buy(3, 100, 30), s);

    auto d = b.depth(5);
    REQUIRE(d.bids.size() == 1);
    REQUIRE(d.bids[0].price == 100);
    REQUIRE(d.bids[0].qty == 60);
    REQUIRE(d.bids[0].order_count == 3);
}

TEST_CASE("depth on empty book", "[depth]") {
    Book b;
    auto d = b.depth(5);
    REQUIRE(d.bids.empty());
    REQUIRE(d.asks.empty());
}

TEST_CASE("depth updates after trade", "[depth]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s); s.clear();
    b.submit(sell(2, 100, 20), s); // partial fill of order 1

    auto d = b.depth(5);
    REQUIRE(d.bids.size() == 1);
    REQUIRE(d.bids[0].price == 100);
    REQUIRE(d.bids[0].qty == 30);
    REQUIRE(d.bids[0].order_count == 1);
}

// --- modify ---

TEST_CASE("modify price of resting order", "[modify]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s); s.clear();

    b.modify(1, 105, 50, s);
    REQUIRE(has<ModifyAck>(s));
    auto& ack = first<ModifyAck>(s);
    REQUIRE(ack.order_id == 1);
    REQUIRE(ack.new_price == 105);
    REQUIRE(ack.new_qty == 50);

    REQUIRE(b.top().best_bid == 105);
}

TEST_CASE("modify qty of resting order", "[modify]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s); s.clear();

    b.modify(1, 100, 80, s);
    REQUIRE(has<ModifyAck>(s));
    REQUIRE(first<ModifyAck>(s).new_qty == 80);
    REQUIRE(b.top().bid_qty == 80);
}

TEST_CASE("modify loses time priority", "[modify]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 40), s);
    b.submit(buy(2, 100, 40), s); s.clear();

    // modify order 1's qty — it should go to the back of the queue at 100
    b.modify(1, 100, 40, s);
    REQUIRE(has<ModifyAck>(s));

    s.clear();
    b.submit(sell(3, 100, 40), s);
    // order 2 now has time priority since order 1 was re-inserted
    REQUIRE(first<Trade>(s).resting_id == 2);
}

TEST_CASE("modify nonexistent order", "[modify]") {
    Book b; VectorSink s;
    b.modify(999, 100, 50, s);
    REQUIRE(has<Reject>(s));
}

TEST_CASE("modify to invalid price", "[modify]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s); s.clear();

    b.modify(1, 0, 50, s);
    REQUIRE(has<Reject>(s));
    REQUIRE_FALSE(has<ModifyAck>(s));
    REQUIRE(b.top().best_bid == 100); // unchanged
    REQUIRE(b.top().bid_qty == 50);
}

TEST_CASE("modify to invalid qty", "[modify]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 50), s); s.clear();

    b.modify(1, 100, -5, s);
    REQUIRE(has<Reject>(s));
    REQUIRE_FALSE(has<ModifyAck>(s));
    REQUIRE(b.top().best_bid == 100); // unchanged
    REQUIRE(b.top().bid_qty == 50);
}

TEST_CASE("modify through exchange", "[modify][exchange]") {
    Exchange ex; VectorSink s;
    ex.add_symbol("AAPL");
    ex.add_symbol("TSLA");

    ex.submit({1, Side::Buy, 150, 100, 1, OrderType::Limit, "AAPL"}, s);
    ex.submit({2, Side::Buy, 250, 50,  2, OrderType::Limit, "TSLA"}, s); s.clear();

    ex.modify(1, 155, 80, s);
    REQUIRE(has<ModifyAck>(s));
    REQUIRE(ex.top("AAPL").best_bid == 155);
    REQUIRE(ex.top("AAPL").bid_qty == 80);
    REQUIRE(ex.top("TSLA").best_bid == 250); // untouched
}

// --- stress / edge cases ---

TEST_CASE("10000 orders at same price, cancel middle", "[stress]") {
    Book b; VectorSink s;
    for (OrderId id = 1; id <= 10000; ++id) {
        b.submit(buy(id, 100, 1), s);
        s.clear();
    }
    REQUIRE(b.order_count() == 10000);

    b.cancel(5000, s);
    REQUIRE(has<CancelAck>(s));
    REQUIRE(b.order_count() == 9999);

    // FIFO for remaining orders: id=1 should still be first
    s.clear();
    b.submit(sell(20000, 100, 1), s);
    REQUIRE(first<Trade>(s).resting_id == 1);
}

TEST_CASE("FOK that needs exactly the full book", "[fok][edge]") {
    Book b; VectorSink s;
    b.submit(sell(1, 100, 40), s);
    b.submit(sell(2, 101, 30), s);
    b.submit(sell(3, 102, 30), s); s.clear();

    b.submit(buy(4, 102, 100, OrderType::FOK), s);
    REQUIRE(has<Ack>(s));
    REQUIRE(has<Filled>(s));
    REQUIRE(trade_count(s) == 3);
    REQUIRE(b.order_count() == 0);
}

TEST_CASE("market order across 100 price levels", "[market][edge]") {
    Book b; VectorSink s;
    for (Price p = 101; p <= 200; ++p) {
        b.submit(sell(static_cast<OrderId>(p), p, 1), s);
    }
    s.clear();

    b.submit(market_buy(9999, 100), s);
    REQUIRE(trade_count(s) == 100);
    REQUIRE(b.order_count() == 0);
}

TEST_CASE("cancel after partial fill", "[cancel][edge]") {
    Book b; VectorSink s;
    b.submit(buy(1, 100, 100), s); s.clear();
    b.submit(sell(2, 100, 30), s);
    REQUIRE(first<Trade>(s).qty == 30);
    REQUIRE(b.top().bid_qty == 70);

    s.clear();
    b.cancel(1, s);
    REQUIRE(has<CancelAck>(s));
    REQUIRE(b.order_count() == 0);
}

TEST_CASE("rapid submit-cancel cycle", "[stress]") {
    Book b; VectorSink s;
    for (OrderId id = 1; id <= 1000; ++id) {
        b.submit(buy(id, 100, 10), s);
        b.cancel(id, s);
        s.clear();
    }
    REQUIRE(b.order_count() == 0);
}
