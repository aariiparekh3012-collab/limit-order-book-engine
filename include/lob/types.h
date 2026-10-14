#pragma once

#include <cstdint>
#include <string>

namespace lob {

// prices in ticks (integer), quantities in whole units. no floats.
using OrderId   = uint64_t;
using TraderId  = uint64_t;
using Price     = int64_t;
using Qty       = int64_t;
using Timestamp = uint64_t;
using Symbol    = std::string;

enum class Side : uint8_t { Buy, Sell };

enum class OrderType : uint8_t {
    Limit,   // rests if not filled
    Market,  // sweeps at any price, never rests
    IOC,     // fill what you can, kill the rest
    FOK      // all or nothing
};

// self-trade prevention modes
enum class STPMode : uint8_t {
    None,           // no STP — self-trades are allowed
    CancelNewest,   // cancel the incoming (aggressor) order
    CancelOldest,   // cancel the resting order
    CancelBoth      // cancel both sides
};

inline const char* to_string(Side s) {
    return s == Side::Buy ? "BUY" : "SELL";
}

inline const char* to_string(OrderType t) {
    switch (t) {
        case OrderType::Limit:  return "LIMIT";
        case OrderType::Market: return "MARKET";
        case OrderType::IOC:    return "IOC";
        case OrderType::FOK:    return "FOK";
    }
    return "?";
}

inline const char* to_string(STPMode m) {
    switch (m) {
        case STPMode::None:         return "NONE";
        case STPMode::CancelNewest: return "CANCEL_NEWEST";
        case STPMode::CancelOldest: return "CANCEL_OLDEST";
        case STPMode::CancelBoth:   return "CANCEL_BOTH";
    }
    return "?";
}

struct Order {
    OrderId   id;
    Side      side;
    Price     price;      // ignored for market orders
    Qty       qty;
    Timestamp ts;
    OrderType type      = OrderType::Limit;
    Symbol    symbol    = "";
    TraderId  trader_id = 0;  // 0 = no trader, STP won't fire
};

} // namespace lob
