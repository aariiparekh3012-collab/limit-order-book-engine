# Limit Order Book Engine

[![CI](https://github.com/aariiparekh3012-collab/limit-order-book-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/aariiparekh3012-collab/limit-order-book-engine/actions/workflows/ci.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=aariiparekh3012-collab_limit-order-book-engine&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=aariiparekh3012-collab_limit-order-book-engine)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A deterministic limit order book and matching engine implemented in C++17. The engine maintains price-time priority, processes multiple order types, supports multiple symbols, and includes testing, snapshot utilities, a command-line interface, and a submission microbenchmark.

## Features

- Price-time priority matching
- Separate bid and ask books
- Multiple-symbol exchange routing
- Limit orders
- Market orders
- Immediate-or-Cancel (IOC) orders
- Fill-or-Kill (FOK) orders
- Order cancellation
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
│   └── bench_submit.cpp
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

## Run the benchmark

```bash
./build/bench_submit
```

The existing benchmark is a single-threaded limit-order submission microbenchmark. It reports submission latency percentiles and aggregate throughput.

Benchmark results depend on the processor, compiler, optimisation flags, operating system, and current system load. Performance figures should therefore be published together with the complete test environment and methodology.

## Benchmark methodology roadmap

The benchmark suite is being extended to include:

- A deterministic mixed-event workload
- Fixed random seed and workload definition
- Warm-up iterations
- Multiple measured iterations
- Median throughput
- p50, p95, and p99 latency
- Peak memory usage
- Final-state correctness verification
- Raw machine-readable results
- One-command reproducibility

See [ROADMAP.md](ROADMAP.md) for planned improvements.

## Correctness

Performance is evaluated only alongside correctness. The test suite covers matching behaviour such as:

- Price priority
- FIFO ordering at the same price
- Partial fills
- Multiple fills
- Market-order behaviour
- IOC behaviour
- FOK behaviour
- Cancellation
- Multi-symbol isolation
- Snapshot and restoration behaviour

## Design

The implementation separates order representation, book-level matching, exchange-level symbol routing, snapshot utilities, and command-line tooling.

For additional implementation details and design decisions, see [DESIGN.md](DESIGN.md).

## Continuous integration

GitHub Actions automatically:

1. Configures the CMake project.
2. Builds using GCC and Clang.
3. Tests Debug and Release configurations.
4. Runs the submission benchmark for Release builds.
5. Performs SonarCloud static analysis in a separate workflow.

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

- Reproducible mixed-workload benchmarks
- Additional cancellation and modification benchmarks
- Benchmark result export
- Expanded snapshot-test integration
- Improved replay tooling
- Additional invariants and property-based tests
- Memory-allocation analysis
- Performance profiling and optimisation

## License

This project is licensed under the [MIT License](LICENSE).
