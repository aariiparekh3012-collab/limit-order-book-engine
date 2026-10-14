#include "lob/cli.h"
#include "lob/exchange.h"
#include "lob/json_sink.h"
#include "lob/log_sanitize.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cstring>
#include <cctype>

// reads orders from a CSV and prints the resulting trade log.
// csv format: id,symbol,side,type,price,qty[,trader_id]
// cancel lines: CANCEL,<order_id>
// modify lines: MODIFY,<order_id>,<new_price>,<new_qty>
// usage: ./lob_cli [--json] [--stp newest|oldest|both] [--depth N] <orders.csv | ->

class PrintSink : public lob::EventSink {
public:
    explicit PrintSink(std::ostream& out) : out_(out) {}

    void on_event(const lob::Event& event) override {
        std::visit([this](const auto& e) { print(e); }, event);
    }
private:
    std::ostream& out_;

    void print(const lob::Ack& e)       { out_ << "  ACK        id=" << e.order_id << "\n"; }
    void print(const lob::Trade& e)     { out_ << "  TRADE      agg=" << e.aggressor_id << " rest=" << e.resting_id << " px=" << e.price << " qty=" << e.qty << "\n"; }
    void print(const lob::Filled& e)    { out_ << "  FILLED     id=" << e.order_id << "\n"; }
    void print(const lob::Partial& e)   { out_ << "  PARTIAL    id=" << e.order_id << " rem=" << e.remaining << "\n"; }
    void print(const lob::CancelAck& e) { out_ << "  CANCEL_ACK id=" << e.order_id << "\n"; }
    void print(const lob::Reject& e)    { out_ << "  REJECT     id=" << e.order_id << " \"" << lob::sanitize_for_log(e.reason) << "\"\n"; }
    void print(const lob::STPCancel& e) { out_ << "  STP_CANCEL agg=" << e.aggressor_id << " rest=" << e.resting_id << " mode=" << lob::to_string(e.mode) << "\n"; }
    void print(const lob::ModifyAck& e) { out_ << "  MODIFY_ACK id=" << e.order_id << " px=" << e.new_price << " qty=" << e.new_qty << "\n"; }
};

static std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    auto b = s.find_last_not_of(" \t\r\n");
    return (a == std::string::npos) ? "" : s.substr(a, b - a + 1);
}

static std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
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

static void show_tob(const lob::Exchange& ex, const std::string& sym,
                     std::ostream& out) {
    auto tob = ex.top(sym);
    out << "  [" << lob::sanitize_for_log(sym) << "] ";
    if (tob.best_bid) out << "bid=" << *tob.best_bid << "x" << tob.bid_qty;
    else              out << "bid=---";
    out << " | ";
    if (tob.best_ask) out << "ask=" << *tob.best_ask << "x" << tob.ask_qty;
    else              out << "ask=---";
    out << "\n";
}

static void show_depth(const lob::Exchange& ex, const std::string& sym, int levels,
                       std::ostream& out) {
    auto d = ex.depth(sym, levels);
    out << "  depth [" << lob::sanitize_for_log(sym) << "]\n";
    size_t n = std::max(d.bids.size(), d.asks.size());
    for (size_t i = 0; i < n; ++i) {
        out << "    ";
        if (i < d.bids.size())
            out << "BID " << d.bids.at(i).price << " x" << d.bids.at(i).qty
                << " (" << d.bids.at(i).order_count << ")";
        else
            out << "BID ---";
        out << "   |   ";
        if (i < d.asks.size())
            out << "ASK " << d.asks.at(i).price << " x" << d.asks.at(i).qty
                << " (" << d.asks.at(i).order_count << ")";
        else
            out << "ASK ---";
        out << "\n";
    }
}

namespace lob::cli {

int run_cli(int argc, char* argv[], std::istream& in, std::ostream& out, std::ostream& err) {
    if (argc < 2) {
        err << "usage: lob_cli [--json] [--stp newest|oldest|both] [--depth N] <orders.csv | ->\n";
        return 1;
    }

    bool json_mode = false;
    lob::STPMode stp_mode = lob::STPMode::None;
    int depth_levels = 0;
    const char* filepath = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) {
            json_mode = true;
        } else if (std::strcmp(argv[i], "--stp") == 0) {
            if (i + 1 >= argc) { err << "--stp needs an argument\n"; return 1; }
            try {
                stp_mode = parse_stp(argv[++i]);
            } catch (const std::exception& e) {
                err << lob::sanitize_for_log(e.what()) << "\n";
                return 1;
            }
        } else if (std::strcmp(argv[i], "--depth") == 0) {
            if (i + 1 >= argc) { err << "--depth needs an argument\n"; return 1; }
            try {
                depth_levels = std::stoi(argv[++i]);
                if (depth_levels < 0) throw std::invalid_argument("negative depth");
            } catch (const std::exception&) {
                err << "--depth requires a non-negative integer\n";
                return 1;
            }
        } else {
            filepath = argv[i];
        }
    }

    if (!filepath) {
        err << "usage: lob_cli [--json] [--stp newest|oldest|both] [--depth N] <orders.csv | ->\n";
        return 1;
    }

    std::ifstream file;
    std::istream* input = &in;
    if (std::string(filepath) != "-") {
        file.open(filepath);
        if (!file) { err << "can't open " << lob::sanitize_for_log(filepath) << "\n"; return 1; }
        input = &file;
    }

    lob::Exchange exchange(stp_mode);
    PrintSink print_sink(out);
    lob::JsonSink json_sink(out);
    lob::EventSink& sink = json_mode
        ? static_cast<lob::EventSink&>(json_sink)
        : static_cast<lob::EventSink&>(print_sink);

    if (!json_mode && stp_mode != lob::STPMode::None)
        out << "-- STP mode: " << lob::to_string(stp_mode) << " --\n\n";

    lob::Timestamp ts = 0;
    std::string line;
    int lineno = 0;

    while (std::getline(*input, line)) {
        ++lineno;
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        if (upper(line).find("ID,") == 0) continue;

        std::istringstream ss(line);
        std::string tok;
        std::vector<std::string> f;
        while (std::getline(ss, tok, ',')) f.push_back(trim(tok));
        if (f.empty()) continue;

        try {
            if (upper(f.at(0)) == "CANCEL") {
                if (f.size() < 2) { err << lineno << ": cancel needs id\n"; continue; }
                auto cid = std::stoull(f.at(1));
                if (!json_mode) out << "CANCEL " << cid << "\n";
                exchange.cancel(cid, sink);
                if (!json_mode) out << "\n";
                continue;
            }

            if (upper(f.at(0)) == "MODIFY") {
                if (f.size() < 4) { err << lineno << ": modify needs id,new_price,new_qty\n"; continue; }
                auto mid       = std::stoull(f.at(1));
                auto new_price = std::stoll(f.at(2));
                auto new_qty   = std::stoll(f.at(3));
                if (!json_mode) out << "MODIFY " << mid << " px=" << new_price << " qty=" << new_qty << "\n";
                exchange.modify(mid, new_price, new_qty, sink);
                if (!json_mode) out << "\n";
                continue;
            }

            if (f.size() < 6) { err << lineno << ": need 6 fields\n"; continue; }

            lob::Order o;
            o.id     = std::stoull(f.at(0));
            o.symbol = trim(f.at(1));
            o.side   = parse_side(f.at(2));
            o.type   = parse_type(f.at(3));
            o.price  = std::stoll(f.at(4));
            o.qty    = std::stoll(f.at(5));
            o.ts     = ++ts;

            if (f.size() >= 7 && !f.at(6).empty())
                o.trader_id = std::stoull(f.at(6));

            if (!exchange.has_symbol(o.symbol)) {
                exchange.add_symbol(o.symbol);
                if (!json_mode)
                    out << "-- new symbol: " << lob::sanitize_for_log(o.symbol) << " --\n";
            }

            if (!json_mode) {
                out << lob::to_string(o.type) << " " << lob::to_string(o.side)
                    << " " << lob::sanitize_for_log(o.symbol) << " id=" << o.id
                    << " px=" << o.price << " qty=" << o.qty;
                if (o.trader_id != 0) out << " trader=" << o.trader_id;
                out << "\n";
            }

            exchange.submit(o, sink);

            if (!json_mode) {
                show_tob(exchange, o.symbol, out);
                if (depth_levels > 0) show_depth(exchange, o.symbol, depth_levels, out);
                out << "\n";
            }
        } catch (const std::exception& e) {
            err << lineno << ": " << lob::sanitize_for_log(e.what()) << "\n";
        }
    }

    return 0;
}

} // namespace lob::cli

#ifndef LOB_DISABLE_MAIN
int main(int argc, char* argv[]) {
    return lob::cli::run_cli(argc, argv, std::cin, std::cout, std::cerr);
}
#endif
