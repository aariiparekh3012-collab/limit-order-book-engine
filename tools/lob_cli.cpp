#include "lob/exchange.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <iomanip>

// reads orders from a CSV and prints the resulting trade log.
// csv format: id,symbol,side,type,price,qty
// cancel lines: CANCEL,<order_id>
// usage: ./lob_cli orders.csv   (or - for stdin)

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

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: lob_cli <orders.csv | ->\n";
        return 1;
    }

    std::istream* in = &std::cin;
    std::ifstream file;
    if (std::string(argv[1]) != "-") {
        file.open(argv[1]);
        if (!file) { std::cerr << "can't open " << argv[1] << "\n"; return 1; }
        in = &file;
    }

    lob::Exchange exchange;
    PrintSink sink;
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
            std::cout << "CANCEL " << cid << "\n";
            exchange.cancel(cid, sink);
            std::cout << "\n";
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

            if (!exchange.has_symbol(o.symbol)) {
                exchange.add_symbol(o.symbol);
                std::cout << "-- new symbol: " << o.symbol << " --\n";
            }

            std::cout << lob::to_string(o.type) << " " << lob::to_string(o.side)
                      << " " << o.symbol << " id=" << o.id
                      << " px=" << o.price << " qty=" << o.qty << "\n";

            exchange.submit(o, sink);
            show_tob(exchange, o.symbol);
            std::cout << "\n";
        } catch (const std::exception& e) {
            std::cerr << lineno << ": " << e.what() << "\n";
        }
    }
}
