#define CATCH_CONFIG_MAIN
#include <catch2/catch_test_macros.hpp>
#include "lob/snapshot.h"

#include <sstream>

using namespace lob;

static uint64_t ts = 1;

Order buy(OrderId id, Price price, Qty qty, TraderId tid = 0) {
    return {id, Side::Buy, price, qty, ts++, OrderType::Limit, "AAPL", tid};
}

Order sell(OrderId id, Price price, Qty qty, TraderId tid = 0) {
    return {id, Side::Sell, price, qty, ts++, OrderType::Limit, "AAPL", tid};
}

// helper: serialize then deserialize
Exchange roundtrip(const Exchange& ex) {
    std::stringstream buf;
    save_snapshot(ex, buf);
    buf.seekg(0);
    return load_snapshot(buf);
}

// helper: serialize to bytes, return as string
std::string to_bytes(const Exchange& ex) {
    std::stringstream buf;
    save_snapshot(ex, buf);
    return buf.str();
}

TEST_CASE("empty exchange round-trips", "[snapshot]") {
    Exchange ex(STPMode::CancelNewest);
    ex.add_symbol("AAPL");
    ex.add_symbol("TSLA");

    auto ex2 = roundtrip(ex);
    REQUIRE(ex2.stp_mode() == STPMode::CancelNewest);
    REQUIRE(ex2.symbol_count() == 2);
    REQUIRE(ex2.has_symbol("AAPL"));
    REQUIRE(ex2.has_symbol("TSLA"));
    REQUIRE(ex2.total_order_count() == 0);
}

TEST_CASE("round-trip preserves resting orders", "[snapshot]") {
    Exchange ex;
    ex.add_symbol("AAPL");
    VectorSink s;

    ex.submit(buy(1, 100, 50), s);
    ex.submit(buy(2, 99, 80), s);
    ex.submit(sell(3, 105, 40), s);
    ex.submit(sell(4, 110, 60), s);

    auto ex2 = roundtrip(ex);
    REQUIRE(ex2.total_order_count() == 4);
    REQUIRE(ex2.top("AAPL").best_bid == 100);
    REQUIRE(ex2.top("AAPL").best_ask == 105);
    REQUIRE(ex2.top("AAPL").bid_qty == 50);
    REQUIRE(ex2.top("AAPL").ask_qty == 40);
}

TEST_CASE("round-trip preserves FIFO within price level", "[snapshot]") {
    Exchange ex;
    ex.add_symbol("AAPL");
    VectorSink s;

    // three bids at same price
    ex.submit(buy(1, 100, 10), s);
    ex.submit(buy(2, 100, 20), s);
    ex.submit(buy(3, 100, 30), s);

    auto ex2 = roundtrip(ex);

    // a sell should match order 1 first (FIFO)
    VectorSink s2;
    ex2.submit({4, Side::Sell, 100, 10, ts++, OrderType::Limit, "AAPL"}, s2);
    bool found_trade = false;
    for (auto& e : s2.events) {
        if (std::holds_alternative<Trade>(e)) {
            REQUIRE(std::get<Trade>(e).resting_id == 1);
            found_trade = true;
        }
    }
    REQUIRE(found_trade);
}

TEST_CASE("serialize-deserialize-serialize gives identical bytes", "[snapshot]") {
    Exchange ex(STPMode::CancelBoth);
    ex.add_symbol("AAPL");
    ex.add_symbol("TSLA");
    VectorSink s;

    ex.submit(buy(10, 150, 100, 42), s);
    ex.submit(sell(11, 160, 50, 99), s);
    ex.submit({12, Side::Buy, 250, 75, ts++, OrderType::Limit, "TSLA", 7}, s);

    auto bytes1 = to_bytes(ex);

    // load and re-save
    std::stringstream buf(bytes1);
    auto ex2 = load_snapshot(buf);
    auto bytes2 = to_bytes(ex2);

    REQUIRE(bytes1 == bytes2);
}

TEST_CASE("cancel works after restore", "[snapshot]") {
    Exchange ex;
    ex.add_symbol("AAPL");
    VectorSink s;

    ex.submit(buy(1, 100, 50), s);
    ex.submit(buy(2, 99, 30), s);

    auto ex2 = roundtrip(ex);
    REQUIRE(ex2.total_order_count() == 2);

    VectorSink s2;
    ex2.cancel(1, s2);
    bool got_cancel = false;
    for (auto& e : s2.events)
        if (std::holds_alternative<CancelAck>(e)) got_cancel = true;
    REQUIRE(got_cancel);
    REQUIRE(ex2.total_order_count() == 1);
}

TEST_CASE("matching works after restore", "[snapshot]") {
    Exchange ex;
    ex.add_symbol("AAPL");
    VectorSink s;

    ex.submit(buy(1, 100, 50), s);
    ex.submit(sell(2, 105, 30), s);

    auto ex2 = roundtrip(ex);

    VectorSink s2;
    ex2.submit({3, Side::Sell, 100, 50, ts++, OrderType::Limit, "AAPL"}, s2);

    bool got_trade = false;
    for (auto& e : s2.events) {
        if (std::holds_alternative<Trade>(e)) {
            auto& t = std::get<Trade>(e);
            REQUIRE(t.resting_id == 1);
            REQUIRE(t.qty == 50);
            got_trade = true;
        }
    }
    REQUIRE(got_trade);
    REQUIRE(ex2.total_order_count() == 1); // sell(2) still resting
}

TEST_CASE("multi-symbol round-trip", "[snapshot]") {
    Exchange ex(STPMode::CancelOldest);
    ex.add_symbol("AAPL");
    ex.add_symbol("TSLA");
    ex.add_symbol("GOOG");
    VectorSink s;

    ex.submit({1, Side::Buy, 150, 100, ts++, OrderType::Limit, "AAPL"}, s);
    ex.submit({2, Side::Sell, 300, 50, ts++, OrderType::Limit, "TSLA"}, s);
    ex.submit({3, Side::Buy, 2800, 10, ts++, OrderType::Limit, "GOOG"}, s);

    auto ex2 = roundtrip(ex);
    REQUIRE(ex2.symbol_count() == 3);
    REQUIRE(ex2.stp_mode() == STPMode::CancelOldest);
    REQUIRE(ex2.total_order_count() == 3);
    REQUIRE(ex2.top("AAPL").best_bid == 150);
    REQUIRE(ex2.top("TSLA").best_ask == 300);
    REQUIRE(ex2.top("GOOG").best_bid == 2800);
}

TEST_CASE("bad magic throws", "[snapshot]") {
    std::stringstream buf("NOPE1234");
    REQUIRE_THROWS_AS(load_snapshot(buf), std::runtime_error);
}

TEST_CASE("truncated stream throws", "[snapshot]") {
    std::stringstream buf("LOB1");
    buf.seekg(0);
    REQUIRE_THROWS_AS(load_snapshot(buf), std::runtime_error);
}

TEST_CASE("event replay produces same state as snapshot", "[snapshot][replay]") {
    // use fixed timestamps so both runs produce identical snapshots
    uint64_t t0 = 1000;

    Exchange ex;
    ex.add_symbol("AAPL");
    VectorSink events;

    ex.submit({100, Side::Buy, 150, 200, t0+1, OrderType::Limit, "AAPL"}, events);
    ex.submit({101, Side::Buy, 148, 100, t0+2, OrderType::Limit, "AAPL"}, events);
    ex.submit({102, Side::Sell, 155, 150, t0+3, OrderType::Limit, "AAPL"}, events);
    ex.submit({103, Side::Sell, 160, 80, t0+4, OrderType::Limit, "AAPL"}, events);
    // this sell crosses the best bid, partial fill
    ex.submit({104, Side::Sell, 150, 120, t0+5, OrderType::Limit, "AAPL"}, events);

    auto snap_bytes = to_bytes(ex);

    // replay from scratch with same timestamps
    Exchange replay_ex;
    replay_ex.add_symbol("AAPL");
    VectorSink replay_sink;

    replay_ex.submit({100, Side::Buy, 150, 200, t0+1, OrderType::Limit, "AAPL"}, replay_sink);
    replay_ex.submit({101, Side::Buy, 148, 100, t0+2, OrderType::Limit, "AAPL"}, replay_sink);
    replay_ex.submit({102, Side::Sell, 155, 150, t0+3, OrderType::Limit, "AAPL"}, replay_sink);
    replay_ex.submit({103, Side::Sell, 160, 80, t0+4, OrderType::Limit, "AAPL"}, replay_sink);
    replay_ex.submit({104, Side::Sell, 150, 120, t0+5, OrderType::Limit, "AAPL"}, replay_sink);

    REQUIRE(ex.total_order_count() == replay_ex.total_order_count());

    auto tob1 = ex.top("AAPL");
    auto tob2 = replay_ex.top("AAPL");
    REQUIRE(tob1.best_bid == tob2.best_bid);
    REQUIRE(tob1.best_ask == tob2.best_ask);
    REQUIRE(tob1.bid_qty == tob2.bid_qty);
    REQUIRE(tob1.ask_qty == tob2.ask_qty);

    // snapshot both — they should produce identical bytes
    auto replay_bytes = to_bytes(replay_ex);
    REQUIRE(snap_bytes == replay_bytes);
}
