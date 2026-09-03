#include "lob/exchange.h"
#include "lob/json_sink.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <iomanip>
#include <cstring>

// reads orders from a CSV and prints the resulting trade log.
// csv format: id,symbol,side,type,price,qty[,trader_id]
// cancel lines: CANCEL,<order_id>
// modify lines: MODIFY,<order_id>,<new_price>,<new_qty>
// usage: ./lob_cli [--json] [--stp newest|oldest|both] [--depth N] <orders.csv | ->

class PrintSink : public lob::EventSink {
public:
    void on_event(const lob::Event& event) override {
        std::visit([](const auto& e) { print(e); }, event);
    }
private:
    static void print(const lob::Ack& e)       { std::cout << "  ACK        id=" << e.order_id << "\n"; }
    static void print(const lob::Trade& e)     { std::cout << "  TRADE      agg=" << e.aggressor_id << " rest=" << e.resting_id << " px=" << e.price << " qty=" << e.qty << "\n"; }
    static void print(const lob::Filled& e)    { std::cout << "  FILLED     id=" << e.order_id << "\n"; }
    static void print(const lob::Partial& e)   { std::cout << "  PARTIAL    id=" << e.order_id << " rem=" << e.remaining << "\n"; }
    static void print(const lob::CancelAck& e) { std::cout << "  CANCEL_ACK id=" << e.order_id << "\n"; }
    static void print(const lob::Reject& e)    { std::cout << "  REJECT     id=" << e.order_id << " \"" << e.reason << "\"\n"; }
    static void print(const lob::STPCancel& e) { std::cout << "  STP_CANCEL agg=" << e.aggressor_id << " rest=" << e.resting_id << " mode=" << lob::to_string(e.mode) << "\n"; }
    static void print(const lob::ModifyAck& e) { std::cout << "  MODIFY_ACK id=" << e.order_id << " px=" << e.new_price << " qty=" << e.new_qty << "\n"; }
};

static std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

static lob::Side parse_side(const std::string& s) {
    auto u = upper(trim(s));
    if (u == "BUY" || u == "B") return lob::Side::Buy;
    if (u == "SELL" || u == "S") return lob::Side::Sell;
    throw std::runtime_error("bad side: " + s);
}

static lob::OrderType parse_type(const std::string& s) {
    auto u = upper(trim(s));
    if (u == "LIMIT" || u == "LMT" || u == "L") return lob::OrderType::Limit;
    if (u == "MARKET" || u == "MKT" || u == "M") return lob::OrderType::Market;
    if (u == "IOC") return lob::OrderType::IOC;
    if (u == "FOK") return lob::OrderType::FOK;
    throw std::runtime_error("bad type: " + s);
}

static lob::STPMode parse_stp(const char* s) {
    std::string u = upper(std::string(s));
    if (u == "NEWEST" || u == "CANCEL_NEWEST") return lob::STPMode::CancelNewest;
    if (u == "OLDEST" || u == "CANCEL_OLDEST") return lob::STPMode::CancelOldest;
    if (u == "BOTH"   || u == "CANCEL_BOTH")   return lob::STPMode::CancelBoth;
    throw std::runtime_error("unknown STP mode: " + std::string(s) + " (use newest, oldest, or both)");
}

static void show_tob(const lob::Exchange& ex, const std::string& sym) {
    auto tob = ex.top(sym);
    std::cout << "  [" << sym << "] ";
    if (tob.best_bid) std::cout << "bid=" << *tob.best_bid << "x" << tob.bid_qty;
    else              std::cout << "bid=---";
    std::cout << " | ";
    if (tob.best_ask) std::cout << "ask=" << *tob.best_ask << "x" << tob.ask_qty;
    else              std::cout << "ask=---";
    std::cout << "\n";
}

static void show_depth(const lob::Exchange& ex, const std::string& sym, int levels) {
    auto d = ex.depth(sym, levels);
    std::cout << "  depth [" << sym << "]\n";
    size_t n = std::max(d.bids.size(), d.asks.size());
    for (size_t i = 0; i < n; ++i) {
        std::cout << "    ";
        if (i < d.bids.size())
            std::cout << "BID " << d.bids[i].price << " x" << d.bids[i].qty
                       << " (" << d.bids[i].order_count << ")";
        else
            std::cout << "BID ---";
        std::cout << "   |   ";
        if (i < d.asks.size())
            std::cout << "ASK " << d.asks[i].price << " x" << d.asks[i].qty
                       << " (" << d.asks[i].order_count << ")";
        else
            std::cout << "ASK ---";
        std::cout << "\n";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: lob_cli [--json] [--stp newest|oldest|both] [--depth N] <orders.csv | ->\n";
        return 1;
    }

    // parse flags
    bool json_mode = false;
    lob::STPMode stp_mode = lob::STPMode::None;
    int depth_levels = 0;
    const char* filepath = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) {
            json_mode = true;
        } else if (std::strcmp(argv[i], "--stp") == 0) {
            if (i + 1 >= argc) { std::cerr << "--stp needs an argument\n"; return 1; }
            stp_mode = parse_stp(argv[++i]);
        } else if (std::strcmp(argv[i], "--depth") == 0) {
            if (i + 1 >= argc) { std::cerr << "--depth needs an argument\n"; return 1; }
            depth_levels = std::stoi(argv[++i]);
        } else {
            filepath = argv[i];
        }
    }

    if (!filepath) {
        std::cerr << "usage: lob_cli [--json] [--stp newest|oldest|both] [--depth N] <orders.csv | ->\n";
        return 1;
    }

    std::istream* in = &std::cin;
    std::ifstream file;
    if (std::string(filepath) != "-") {
        file.open(filepath);
        if (!file) { std::cerr << "can't open " << filepath << "\n"; return 1; }
        in = &file;
    }

    lob::Exchange exchange(stp_mode);

    // pick the sink
    PrintSink print_sink;
    lob::JsonSink json_sink(std::cout);
    lob::EventSink& sink = json_mode
        ? static_cast<lob::EventSink&>(json_sink)
        : static_cast<lob::EventSink&>(print_sink);

    if (!json_mode && stp_mode != lob::STPMode::None)
        std::cout << "-- STP mode: " << lob::to_string(stp_mode) << " --\n\n";

    lob::Timestamp ts = 0;
    std::string line;
    int lineno = 0;

    while (std::getline(*in, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (upper(line).find("ID,") == 0) continue; // header

        std::istringstream ss(line);
        std::string tok;
        std::vector<std::string> f;
        while (std::getline(ss, tok, ',')) f.push_back(trim(tok));
        if (f.empty()) continue;

        if (upper(f[0]) == "CANCEL") {
            if (f.size() < 2) { std::cerr << lineno << ": cancel needs id\n"; continue; }
            auto cid = std::stoull(f[1]);
            if (!json_mode) std::cout << "CANCEL " << cid << "\n";
            exchange.cancel(cid, sink);
            if (!json_mode) std::cout << "\n";
            continue;
        }

        if (upper(f[0]) == "MODIFY") {
            if (f.size() < 4) { std::cerr << lineno << ": modify needs id,new_price,new_qty\n"; continue; }
            auto mid       = std::stoull(f[1]);
            auto new_price = std::stoll(f[2]);
            auto new_qty   = std::stoll(f[3]);
            if (!json_mode) std::cout << "MODIFY " << mid << " px=" << new_price << " qty=" << new_qty << "\n";
            exchange.modify(mid, new_price, new_qty, sink);
            if (!json_mode) std::cout << "\n";
            continue;
        }

        if (f.size() < 6) { std::cerr << lineno << ": need 6 fields\n"; continue; }

        try {
            lob::Order o;
            o.id     = std::stoull(f[0]);
            o.symbol = trim(f[1]);
            o.side   = parse_side(f[2]);
            o.type   = parse_type(f[3]);
            o.price  = std::stoll(f[4]);
            o.qty    = std::stoll(f[5]);
            o.ts     = ++ts;

            // optional 7th field: trader_id
            if (f.size() >= 7 && !f[6].empty())
                o.trader_id = std::stoull(f[6]);

            if (!exchange.has_symbol(o.symbol)) {
                exchange.add_symbol(o.symbol);
                if (!json_mode)
                    std::cout << "-- new symbol: " << o.symbol << " --\n";
            }

            if (!json_mode) {
                std::cout << lob::to_string(o.type) << " " << lob::to_string(o.side)
                          << " " << o.symbol << " id=" << o.id
                          << " px=" << o.price << " qty=" << o.qty;
                if (o.trader_id != 0) std::cout << " trader=" << o.trader_id;
                std::cout << "\n";
            }

            exchange.submit(o, sink);

            if (!json_mode) {
                show_tob(exchange, o.symbol);
                if (depth_levels > 0) show_depth(exchange, o.symbol, depth_levels);
                std::cout << "\n";
            }
        } catch (const std::exception& e) {
            std::cerr << lineno << ": " << e.what() << "\n";
        }
    }
}
