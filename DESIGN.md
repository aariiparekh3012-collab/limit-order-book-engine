# design notes

## data structures

each side of the book is a `std::map<Price, std::list<RestingOrder>>`. bids use `std::greater` (highest first), asks use default `std::less` (lowest first). a separate `std::unordered_map<OrderId, list::iterator>` gives O(1) cancel by storing a stable iterator into the queue.

this is correct and simple. a faster approach (flat array of price levels + intrusive list) would avoid allocator overhead and be more cache-friendly, but the interface is the same so it's a drop-in replacement later.

## order types

- **limit**: matches if price crosses, remainder rests on book
- **market**: matches at any available price, never rests. unfilled portion is cancelled.
- **IOC** (immediate-or-cancel): like limit but remainder is cancelled instead of resting
- **FOK** (fill-or-kill): atomic — checks if the full qty is available before matching. if not, rejected without touching the book.

## matching invariants

1. best bid < best ask at rest (no crossed book)
2. within a price level, orders match in FIFO order
3. no trade-through: a resting order can't be skipped for a worse-priced one
4. quantity conservation: qty_in = qty_matched + qty_resting + qty_cancelled

the test suite checks these after every operation.

## event model

every operation emits events in order: ACK → TRADE(s) → FILLED/PARTIAL → CANCEL_ACK/REJECT. the event log is the ground truth — state is reconstructable from replay.

## multi-symbol

`Exchange` holds an `unordered_map<Symbol, Book>`. routes `submit()` by `order.symbol`, rejects unknown symbols. cancel does a linear scan across books (fine for small symbol counts; could add a global id→symbol index if needed).

## public api

```cpp
class Book {
    void submit(const Order& o, EventSink& sink);
    void cancel(OrderId id, EventSink& sink);
    TopOfBook top() const;
};

class Exchange {
    void add_symbol(const Symbol& sym);
    void submit(const Order& o, EventSink& sink);
    void cancel(OrderId id, EventSink& sink);
    TopOfBook top(const Symbol& sym) const;
};
```

## benchmark

1M random orders (price range 990-1010, qty 1-100, 50/50 buy/sell), single-threaded, -O2:
- p50: ~275ns
- p99: ~1µs
- throughput: ~2.6M orders/sec

## what's not here yet

self-trade prevention, stop/iceberg orders, persistence, multithreading, networking, fees, risk checks. these are potential SLP extensions.
