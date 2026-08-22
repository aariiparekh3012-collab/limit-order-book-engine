#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "lob/book.h"
#include "lob/exchange.h"

using namespace lob;

static uint64_t next_ts = 1;

Order buy(OrderId id, Price price, Qty qty, OrderType type = OrderType::Limit) {
    return {id, Side::Buy, price, qty, next_ts++, type};
}

Order sell(OrderId id, Price price, Qty qty, OrderType type = OrderType::Limit) {
    return {id, Side::Sell, price, qty, next_ts++, type};
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
