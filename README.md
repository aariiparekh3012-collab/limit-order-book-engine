# the-book-was-open

C++17 limit order book matching engine. price-time priority, multi-symbol, four order types, and sub-microsecond latency. allegedly.

---

## why

every exchange needs a matching engine at its core — something that takes buy and sell orders, figures out which ones cross, and spits out trades. this is that thing, minus the compliance department and the $200k bloomberg terminal.

it's single-threaded, deterministic, and event-sourced. no floats anywhere. prices are integer ticks, quantities are whole units. the kind of thing you'd actually want at the bottom of a trading stack.

## what it does

**four order types:**

- **limit** — rests on the book if it doesn't fill. the bread and butter.
- **market** — sweeps whatever's available, cancels the rest. never rests.
- **IOC** (immediate-or-cancel) — like limit, but if it doesn't fully fill, the remainder dies.
- **FOK** (fill-or-kill) — atomic. checks if the full quantity is available *before* touching the book. if not, rejected outright.

**matching rules:**

- strict price-time priority (best price first, FIFO within each level)
- trades execute at the resting order's price
- no trade-through: can't skip a resting order for a worse-priced one
- book is never crossed at rest (best bid < best ask, always)

**multi-symbol** via an `Exchange` class that routes orders to per-symbol `Book` instances. register symbols, submit orders, get trades. cancel searches globally by order ID.

**event-sourced** — every operation emits a sequence of events: `ACK → TRADE(s) → FILLED/PARTIAL → CANCEL_ACK/REJECT`. state is fully reconstructable from the event log.

## how it works

```
                    ┌─────────────┐
    Order ────────► │  Exchange   │ ──── routes by symbol
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │    Book     │ ──── one per symbol
                    ├─────────────┤
                    │  bids_      │  std::map<Price, Queue, std::greater>
                    │  asks_      │  std::map<Price, Queue>
                    │  index_     │  std::unordered_map<OrderId, Iterator>
                    └──────┬──────┘
                           │
                    ┌──────▼──────┐
                    │  EventSink  │ ──── ACK, TRADE, FILLED, ...
                    └─────────────┘
```

each side of the book is a sorted map of price levels → FIFO queues (`std::list`). bids are sorted high-to-low (`std::greater`), asks low-to-high. a separate hash map stores stable iterators for O(1) cancel lookup.

FOK orders use a read-only `can_fill()` that walks the opposite side without modifying anything. if the check passes, the actual match is guaranteed to fully fill — atomic semantics without transactions.

## performance

```
$ ./build/bench_submit
submit benchmark (1000000 orders, 10000 warmup)
  p50:   263 ns
  p90:   506 ns
  p99:   1039 ns
  p99.9: 5250 ns
  avg:   357 ns
  throughput: 2795738 orders/sec
```

1M random orders, single-threaded, `-O2`, price range ±10 ticks around mid. the data structures are textbook (red-black tree + linked list), not yet optimized with flat arrays or custom allocators — so there's room to go faster.

## build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

requires C++17 and cmake 3.14+. tested on gcc-12 and clang-15.

## run

```bash
# tests (30 of them)
ctest --test-dir build --output-on-failure

# benchmark
./build/bench_submit

# CLI — feed it a CSV, get a trade log
./build/lob_cli examples/sample_orders.csv
```

the CLI reads `id,symbol,side,type,price,qty` lines, auto-registers new symbols, and prints events + top-of-book after each order. supports `CANCEL,<id>` lines. pass `-` for stdin.

**example output:**
```
LIMIT BUY AAPL id=1 px=15000 qty=100
  ACK        id=1
  [AAPL] bid=15000x100 | ask=---

LIMIT SELL AAPL id=5 px=15000 qty=50
  ACK        id=5
  TRADE      agg=5 rest=1 px=15000 qty=50
  FILLED     id=5
  [AAPL] bid=15000x50 | ask=15100x150

IOC SELL AAPL id=7 px=14800 qty=500
  ACK        id=7
  TRADE      agg=7 rest=1 px=15000 qty=50
  FILLED     id=1
  TRADE      agg=7 rest=2 px=14900 qty=200
  FILLED     id=2
  PARTIAL    id=7 rem=250
  CANCEL_ACK id=7
```

## project layout

```
include/lob/
  types.h         OrderId, Price, Qty, Side, OrderType, Order
  event.h         Ack, Trade, Filled, Partial, CancelAck, Reject, EventSink
  book.h          single-symbol order book
  exchange.h      multi-symbol router

src/
  book.cpp        matching engine (~180 lines)

tests/
  test_matching.cpp   30 catch2 tests — limits, market, IOC, FOK, cancel, exchange

bench/
  bench_submit.cpp    latency benchmark (1M orders, percentile reporting)

tools/
  lob_cli.cpp         CSV → trade log

examples/
  sample_orders.csv   demo scenarios
```

## what's not here

self-trade prevention. stop orders. iceberg orders. persistence. multithreading. networking. risk checks. FIX protocol. a GUI. a compliance department.

some of these are planned. most are deliberately out of scope — this is a matching engine, not an exchange.

## design

see [DESIGN.md](DESIGN.md) for data structure rationale, matching invariants, and the "what could be faster" section.

## license

MIT
