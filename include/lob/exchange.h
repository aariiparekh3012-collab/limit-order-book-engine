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

        // Fast-path: if a recent submit or cancel has already mapped this order
        // to a specific book, use it directly. This avoids a full scan across
        // every symbol in the common case and keeps the API semantics unchanged.
        if (order_index_.count(order.id)) {
            sink.on_event(Reject{order.id, "duplicate order id"});
            return;
        }

        it->second.submit(order, sink);

        // The book may have filled the incoming order and left it inactive; in
        // that case, do not keep a stale entry. If the order remains resting,
        // cache its home book for the cheaper cancel/modify path. We still fall
        // back to scanning all books if the map is stale or missing.
        if (it->second.contains(order.id)) {
            order_index_[order.id] = &it->second;
        } else {
            order_index_.erase(order.id);
        }
    }

    // Cancel prefers the fast path through the global order index. If stale or
    // missing, it falls back to the old O(number of symbols) search.
    void cancel(OrderId id, EventSink& sink) {
        auto it = order_index_.find(id);
        if (it != order_index_.end()) {
            it->second->cancel(id, sink);
            order_index_.erase(id);
            return;
        }

        for (auto& [sym, book] : books_) {
            if (book.contains(id)) {
                book.cancel(id, sink);
                order_index_.erase(id);
                return;
            }
        }
        sink.on_event(Reject{id, "order not found"});
    }

    // Modify prefers the fast path through the global order index. If stale or
    // missing, it falls back to the old O(number of symbols) search.
    void modify(OrderId id, Price new_price, Qty new_qty, EventSink& sink) {
        auto it = order_index_.find(id);
        if (it != order_index_.end()) {
            it->second->modify(id, new_price, new_qty, sink);
            return;
        }

        for (auto& [sym, book] : books_) {
            if (book.contains(id)) {
                book.modify(id, new_price, new_qty, sink);
                order_index_[id] = &book;
                return;
            }
        }
        sink.on_event(Reject{id, "order not found"});
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

    // Fast path for cancel/modify on active orders. This index is intentionally
    // best-effort: the engine can remove orders during matching without the
    // exchange knowing which order ID disappeared, so cancel/modify falls back to
    // scanning all books if the entry is missing or stale.
    std::unordered_map<OrderId, Book*> order_index_;
};

} // namespace lob
