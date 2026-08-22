#pragma once

#include <cstdint>
#include <string>

namespace lob {

// prices in ticks (integer), quantities in whole units. no floats.
using OrderId   = uint64_t;
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

struct Order {
    OrderId   id;
    Side      side;
    Price     price;      // ignored for market orders
    Qty       qty;
    Timestamp ts;
    OrderType type   = OrderType::Limit;
    Symbol    symbol = "";
};

} // namespace lob
