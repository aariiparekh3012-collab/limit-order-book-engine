#pragma once

#include "types.h"
#include "book.h"

#include <unordered_map>
#include <vector>
#include <stdexcept>

namespace lob {

// manages one Book per symbol, routes by order.symbol
class Exchange {
public:
    explicit Exchange(STPMode stp = STPMode::None) : stp_mode_(stp) {}

    void add_symbol(const Symbol& sym) {
        books_.emplace(sym, Book{stp_mode_});
    }

    bool has_symbol(const Symbol& sym) const {
        return books_.count(sym) > 0;
    }

    void submit(const Order& order, EventSink& sink) {
        auto it = books_.find(order.symbol);
        if (it == books_.end()) {
            sink.on_event(Reject{order.id, "unknown symbol: " + order.symbol});
            return;
        }
        it->second.submit(order, sink);
    }

    // cancel searches all books — order IDs are globally unique
    // TODO: if this becomes a bottleneck, add a global id->symbol index
    void cancel(OrderId id, EventSink& sink) {
        for (auto& [sym, book] : books_) {
            if (book.order_count() == 0) continue;

            VectorSink probe;
            book.cancel(id, probe);

            // if we got a CancelAck, it was found — forward and return
            for (const auto& e : probe.events) {
                if (std::holds_alternative<CancelAck>(e)) {
                    for (const auto& ev : probe.events) sink.on_event(ev);
                    return;
                }
            }
            // otherwise it was a reject from this book, try the next one
        }
        sink.on_event(Reject{id, "order not found"});
    }

    TopOfBook top(const Symbol& sym) const {
        auto it = books_.find(sym);
        if (it == books_.end()) return {};
        return it->second.top();
    }

    const Book& book(const Symbol& sym) const {
        auto it = books_.find(sym);
        if (it == books_.end())
            throw std::runtime_error("unknown symbol: " + sym);
        return it->second;
    }

    STPMode stp_mode() const { return stp_mode_; }
    size_t symbol_count() const { return books_.size(); }

    size_t total_order_count() const {
        size_t n = 0;
        for (const auto& [_, b] : books_) n += b.order_count();
        return n;
    }

    // snapshot: list all active symbols
    std::vector<Symbol> symbols() const {
        std::vector<Symbol> out;
        out.reserve(books_.size());
        for (auto& [sym, _] : books_) out.push_back(sym);
        return out;
    }

    // mutable book access for restore
    Book& mutable_book(const Symbol& sym) {
        auto it = books_.find(sym);
        if (it == books_.end())
            throw std::runtime_error("unknown symbol: " + sym);
        return it->second;
    }

private:
    STPMode stp_mode_;
    std::unordered_map<Symbol, Book> books_;
};

} // namespace lob
