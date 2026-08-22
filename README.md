# lob-matching-engine

Limit order book matching engine in C++17. Supports limit, market, IOC, and FOK orders across multiple symbols. Price-time priority, integer-only arithmetic, no external dependencies in the core.

## what it does

- price-time priority matching (FIFO within each price level)
- order types: limit, market, immediate-or-cancel, fill-or-kill
- multi-symbol via `Exchange` class that routes orders to per-symbol books
- event-sourced: every operation emits events (ACK, TRADE, FILLED, PARTIAL, CANCEL_ACK, REJECT)
- all prices in ticks, all quantities in whole units — no floats anywhere
- CLI tool that reads orders from CSV and prints the trade log

## build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build        # 30 tests
./build/bench_submit          # latency benchmark
./build/lob_cli examples/sample_orders.csv
```

## benchmark

1M random orders, single-threaded, p50 ~275ns, throughput ~2.6M orders/sec. See `bench/bench_submit.cpp`.

## layout

```
include/lob/types.h       core types (OrderId, Price, Qty, Side, OrderType)
include/lob/event.h       event definitions and sink interface
include/lob/book.h         single-symbol order book
include/lob/exchange.h     multi-symbol router
src/book.cpp               matching engine implementation
tests/test_matching.cpp    catch2 tests (limit, market, IOC, FOK, cancel, exchange)
bench/bench_submit.cpp     latency benchmark
tools/lob_cli.cpp          CSV → trade log CLI
examples/sample_orders.csv demo order flow
```

## design

see [DESIGN.md](DESIGN.md) for data structure choices and matching invariants.

## license

MIT
