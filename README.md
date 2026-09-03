# Limit Order Book Engine

[![CI](https://github.com/aariiparekh3012-collab/limit-order-book-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/aariiparekh3012-collab/limit-order-book-engine/actions/workflows/ci.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=aariiparekh3012-collab_limit-order-book-engine&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=aariiparekh3012-collab_limit-order-book-engine)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A deterministic limit order book and matching engine implemented in C++17. The engine maintains price-time priority, processes multiple order types, supports order modification and L2 market depth, supports multiple symbols, and includes testing, snapshot utilities, a command-line interface, and both a submission and a mixed-workload microbenchmark.

## Features

- Price-time priority matching
- Separate bid and ask books
- Multiple-symbol exchange routing
- Limit orders
- Market orders
- Immediate-or-Cancel (IOC) orders
- Fill-or-Kill (FOK) orders
- Order cancellation
- Order modification (reprice and/or requantity, passive, loses time priority)
- L2 market depth (aggregated qty and order count per price level, both sides)
- Partial and complete fills
- Deterministic trade generation
- Snapshot serialization and restoration
- CSV-driven command-line interface
- Automated GCC and Clang builds
- Debug and Release testing
- SonarCloud static analysis
- C++17 implementation

## Matching rules

The engine applies the following rules:

1. The best price receives priority.
2. At the same price, the earliest order receives priority.
3. Incoming buy orders match the lowest available ask.
4. Incoming sell orders match the highest available bid.
5. An order may execute partially across multiple resting orders.
6. Unfilled limit-order quantity may rest in the book.
7. Unfilled market and IOC quantity is cancelled.
8. FOK orders execute only when the complete quantity can be filled immediately.

## Order modification

`modify(order_id, new_price, new_qty)` reprices and/or requantities a
resting order in place:

- It is a passive operation: the new price is never matched against the
  opposite side, even if it would cross. It only changes what's resting.
- It loses time priority. Internally a modify removes the order and
  re-inserts it at the back of its (possibly new) price level's queue, so
  two orders resting at the same price where the first is modified will
  have the second execute first on the next incoming match.
- Invalid input (`new_qty <= 0`, `new_price <= 0`, or an unknown order id)
  is rejected and the resting order is left completely untouched.
- `Book::modify` and `Exchange::modify` both emit a `ModifyAck{order_id,
  new_price, new_qty}` on success.

## L2 market depth

`Book::depth(levels)` / `Exchange::depth(symbol, levels)` return up to
`levels` aggregated price levels per side (best first): total resting
quantity and order count at each price, without exposing individual order
ids. Pass `--depth N` to `lob_cli` to print it after every order.

## Order types

| Order type | Behaviour |
|---|---|
| Limit | Executes at the specified price or better; remaining quantity may rest |
| Market | Executes immediately against available liquidity; never rests |
| IOC | Executes immediately and cancels any unfilled quantity |
| FOK | Executes completely and immediately or is rejected without a partial fill |

## Project structure

```text
.
├── .github/
│   └── workflows/
│       ├── ci.yml
│       └── main.yml
├── bench/
│   ├── bench_submit.cpp
│   └── bench_mixed.cpp
├── examples/
│   └── sample_orders.csv
├── include/
│   └── lob/
├── src/
│   └── book.cpp
├── tests/
│   ├── test_matching.cpp
│   └── test_snapshot.cpp
├── tools/
│   └── lob_cli.cpp
├── CMakeLists.txt
├── DESIGN.md
├── ROADMAP.md
├── sonar-project.properties
└── LICENSE
```

## Requirements

- CMake 3.16 or later
- A C++17-compatible compiler
- GCC or Clang
- Git

## Build

Clone the repository:

```bash
git clone https://github.com/aariiparekh3012-collab/limit-order-book-engine.git
cd limit-order-book-engine
```

Configure a Release build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Compile:

```bash
cmake --build build --parallel
```

## Run the tests

```bash
ctest --test-dir build --output-on-failure
```

The CI workflow performs the build and test suite using:

- GCC Debug
- GCC Release
- Clang Debug
- Clang Release

## Run the command-line interface

A sample order file is provided in `examples/sample_orders.csv`.

```bash
./build/lob_cli examples/sample_orders.csv
```

The CLI can be used to submit a reproducible sequence of orders and inspect the resulting trades and order-book state.

## Run the benchmarks

```bash
./build/bench_submit
./build/bench_mixed
```

`bench_submit` is a single-threaded limit-order submission microbenchmark: 1M orders after a 10k-order warmup, prices clustered tightly around a mid so most orders rest rather than cross repeatedly. It reports submission latency percentiles and aggregate throughput.

`bench_mixed` models a more realistic order flow instead of pure insertion: after the same 10k-order warmup, it runs 1M operations split 60% limit submits (50/50 buy/sell, price clustered around a mid), 25% cancels (of a randomly chosen currently-resting order), and 15% modifies (price nudged ±1-3 ticks with a fresh random quantity, exercising the loses-time-priority reprice path). Latency is reported separately per operation type — a cancel is an O(1) list erase while a submit may walk several price levels, so averaging them together would hide that difference — plus one aggregate ops/sec throughput number across all three types combined. Both the workload mix and the RNG seed are fixed, so a given build reproduces the same sequence of operations run to run.

Benchmark results depend on the processor, compiler, optimisation flags, operating system, and current system load. Performance figures should therefore be published together with the complete test environment and methodology.

See [ROADMAP.md](ROADMAP.md) for further planned improvements (result export, peak-memory tracking, raw machine-readable output).

## Correctness

Performance is evaluated only alongside correctness. 62 test cases across two Catch2 executables (`test_matching`, 52 cases; `test_snapshot`, 10 cases) cover matching behaviour such as:

- Price priority
- FIFO ordering at the same price
- Partial fills
- Multiple fills
- Market-order behaviour
- IOC behaviour
- FOK behaviour
- Self-trade prevention (all three modes)
- Cancellation
- Order modification, including loss of time priority and rejection paths
- L2 market depth, including aggregation and post-trade updates
- Multi-symbol isolation
- Snapshot and restoration behaviour
- Stress/edge cases: 10,000 resting orders, sweeps across 100 price levels, rapid submit/cancel cycles

## Design

The implementation separates order representation, book-level matching, exchange-level symbol routing, snapshot utilities, and command-line tooling.

For additional implementation details and design decisions, see [DESIGN.md](DESIGN.md).

## Continuous integration

GitHub Actions automatically:

1. Configures the CMake project.
2. Builds using GCC and Clang.
3. Tests Debug and Release configurations.
4. Generates a code coverage report (via `lcov`/`gcov`) for the GCC Debug build.
5. Runs both benchmarks for Release builds.
6. Performs SonarCloud static analysis in a separate workflow.

## Current scope

This project focuses on the in-memory matching-engine core. It is not a production exchange and does not currently provide:

- Network connectivity
- Authentication or account management
- Persistence guarantees
- Distributed sequencing
- Risk management
- Production market-data feeds
- Fault-tolerant deployment
- Regulatory controls

## Future work

Planned improvements include:

- Benchmark result export
- Expanded snapshot-test integration
- Improved replay tooling
- Additional invariants and property-based tests
- Memory-allocation analysis
- Performance profiling and optimisation

## License

This project is licensed under the [MIT License](LICENSE).
