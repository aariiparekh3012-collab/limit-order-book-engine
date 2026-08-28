#pragma once

#include "types.h"
#include "exchange.h"

#include <iostream>
#include <cstring>
#include <algorithm>
#include <stdexcept>

namespace lob {

// binary snapshot format (v1):
//
//   [4 bytes]  magic "LOB1"
//   [1 byte]   version (1)
//   [1 byte]   stp_mode
//   [4 bytes]  num_symbols (uint32)
//   for each symbol:
//     [2 bytes]   symbol_len (uint16)
//     [N bytes]   symbol string (no null terminator)
//     [4 bytes]   num_orders (uint32)
//     for each order:
//       [8 bytes]  id        (uint64)
//       [8 bytes]  trader_id (uint64)
//       [1 byte]   side      (0=buy, 1=sell)
//       [8 bytes]  price     (int64)
//       [8 bytes]  remaining (int64)
//       [8 bytes]  timestamp (uint64)
//
// total per order: 41 bytes. all integers little-endian (we just memcpy
// and assume the same platform for now — good enough for a student project,
// and we document the assumption).

namespace detail {

template<typename T>
inline void write_raw(std::ostream& out, const T& val) {
    out.write(reinterpret_cast<const char*>(&val), sizeof(T));
}

template<typename T>
inline T read_raw(std::istream& in) {
    T val;
    in.read(reinterpret_cast<char*>(&val), sizeof(T));
    if (!in) throw std::runtime_error("snapshot: unexpected end of stream");
    return val;
}

inline void write_str(std::ostream& out, const std::string& s) {
    uint16_t len = static_cast<uint16_t>(s.size());
    write_raw(out, len);
    out.write(s.data(), len);
}

inline std::string read_str(std::istream& in) {
    auto len = read_raw<uint16_t>(in);
    std::string s(len, '\0');
    in.read(&s[0], len);
    if (!in) throw std::runtime_error("snapshot: truncated string");
    return s;
}

} // namespace detail

// serialize the full exchange state to a binary stream
inline void save_snapshot(const Exchange& ex, std::ostream& out) {
    using namespace detail;

    // header
    out.write("LOB1", 4);
    uint8_t version = 1;
    write_raw(out, version);
    uint8_t stp = static_cast<uint8_t>(ex.stp_mode());
    write_raw(out, stp);

    auto syms = ex.symbols();
    std::sort(syms.begin(), syms.end()); // deterministic order
    uint32_t nsyms = static_cast<uint32_t>(syms.size());
    write_raw(out, nsyms);

    for (auto& sym : syms) {
        write_str(out, sym);

        auto orders = ex.book(sym).dump_orders();
        uint32_t norders = static_cast<uint32_t>(orders.size());
        write_raw(out, norders);

        for (auto& ro : orders) {
            write_raw(out, ro.id);
            write_raw(out, ro.trader_id);
            uint8_t side = static_cast<uint8_t>(ro.side);
            write_raw(out, side);
            write_raw(out, ro.price);
            write_raw(out, ro.remaining);
            write_raw(out, ro.ts);
        }
    }
}

// deserialize: builds a new Exchange from the binary stream
inline Exchange load_snapshot(std::istream& in) {
    using namespace detail;

    char magic[4];
    in.read(magic, 4);
    if (!in || std::memcmp(magic, "LOB1", 4) != 0)
        throw std::runtime_error("snapshot: bad magic");

    auto version = read_raw<uint8_t>(in);
    if (version != 1)
        throw std::runtime_error("snapshot: unsupported version");

    auto stp = static_cast<STPMode>(read_raw<uint8_t>(in));
    Exchange ex(stp);

    auto nsyms = read_raw<uint32_t>(in);
    for (uint32_t i = 0; i < nsyms; i++) {
        auto sym = read_str(in);
        ex.add_symbol(sym);
        auto& book = ex.mutable_book(sym);

        auto norders = read_raw<uint32_t>(in);
        for (uint32_t j = 0; j < norders; j++) {
            RestingOrder ro;
            ro.id        = read_raw<uint64_t>(in);
            ro.trader_id = read_raw<uint64_t>(in);
            ro.side      = static_cast<Side>(read_raw<uint8_t>(in));
            ro.price     = read_raw<int64_t>(in);
            ro.remaining = read_raw<int64_t>(in);
            ro.ts        = read_raw<uint64_t>(in);
            book.restore_order(ro);
        }
    }

    return ex;
}

} // namespace lob
