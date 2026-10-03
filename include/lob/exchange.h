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

        if (order_index_.count(order.id)) {
            sink.on_event(Reject{order.id, "duplicate order id"});
            return;
        }

        it->second.submit(order, sink);

        if (it->second.contains(order.id)) {
            order_index_[order.id] = &it->second;
        } else {
            order_index_.erase(order.id);
        }
    }

    void cancel(OrderId id, EventSink& sink) {
        Book* book = find_book(id);
        if (book == nullptr) {
            sink.on_event(Reject{id, "order not found"});
            return;
        }

        book->cancel(id, sink);
        order_index_.erase(id);
    }

    void modify(OrderId id, Price new_price, Qty new_qty, EventSink& sink) {
        Book* book = find_book(id);
        if (book == nullptr) {
            sink.on_event(Reject{id, "order not found"});
            return;
        }

        book->modify(id, new_price, new_qty, sink);
    }

    TopOfBook top(const Symbol& sym) const {
        auto it = books_.find(sym);
        if (it == books_.end()) return {};
        return it->second.top();
    }

    MarketDepth depth(const Symbol& sym, int levels) const {
        auto it = books_.find(sym);
        if (it == books_.end()) return {};
        return it->second.depth(levels);
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

    std::vector<Symbol> symbols() const {
        std::vector<Symbol> out;
        out.reserve(books_.size());
        for (auto& [sym, _] : books_) out.push_back(sym);
        return out;
    }

    Book& mutable_book(const Symbol& sym) {
        auto it = books_.find(sym);
        if (it == books_.end())
            throw std::runtime_error("unknown symbol: " + sym);
        return it->second;
    }

private:
    STPMode stp_mode_;
    std::unordered_map<Symbol, Book> books_;
    std::unordered_map<OrderId, Book*> order_index_;

    Book* find_book(OrderId id) {
        auto it = order_index_.find(id);
        if (it != order_index_.end())
            return it->second;

        for (auto& [_, book] : books_) {
            if (book.contains(id)) {
                order_index_[id] = &book;
                return &book;
            }
        }

        return nullptr;
    }
};

} // namespace lob
