# Limit Order Book Engine

[![XERJ Corpus Hub contributor](https://hub.xerj.org/contributors/aariiparekh3012-collab/badge.svg)](https://hub.xerj.org/contributors/aariiparekh3012-collab)

[![CI](https://github.com/aariiparekh3012-collab/limit-order-book-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/aariiparekh3012-collab/limit-order-book-engine/actions/workflows/ci.yml)
[![Quality Gate Status](https://sonarcloud.io/api/project_badges/measure?project=aariiparekh3012-collab_limit-order-book-engine&metric=alert_status)](https://sonarcloud.io/summary/new_code?id=aariiparekh3012-collab_limit-order-book-engine)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](https://en.cppreference.com/w/cpp/17)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A deterministic limit order book and matching engine implemented in C++17.

**[Open the interactive replay lab](https://aariiparekh3012-collab.github.io/limit-order-book-engine/)** to scrub a deterministic order stream, inspect L2 depth and examine execution VWAP and implementation shortfall for an FOK research order. The engine maintains price-time priority, processes multiple order types, supports order modification and L2 market depth, supports multiple symbols, and includes testing, snapshot utilities, a command-line interface, and both a submission and a mixed-workload microbenchmark. The demo is also available locally at [`demo/index.html`](demo/index.html).

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

## Implementation details

The current engine stores price-level buckets in `std::unordered_map`, with FIFO queues implemented as intrusive doubly linked lists. Order IDs map to node pointers for average O(1) direct lookup, while a free list recycles detached nodes. Price keys are explicitly sorted for matching and L2 depth; best bid/ask discovery scans occupied levels. See [DESIGN.md](DESIGN.md) for the actual algorithmic costs and tradeoffs.

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
│       ├── main.yml
│       └── pages.yml
├── bench/
│   ├── benchmark_csv.hpp
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

Pass `--csv` to either benchmark to emit a stable, machine-readable schema instead of the human-readable report. Redirect the output to keep results for build-to-build comparisons:

```bash
./build/bench_submit --csv > benchmark-submit.csv
./build/bench_mixed --csv > benchmark-mixed.csv
```

Both files use the same tidy schema. The submission benchmark emits one latency row; the mixed benchmark emits one latency row for each operation type plus an `all` row containing aggregate wall-clock throughput. Empty fields are not applicable to that row. `schema_version` allows readers to reject incompatible future formats, and the fixed RNG seed and warmup count are included with every row. Release CI runs upload both CSV files as compiler-specific workflow artifacts.

`bench_submit` is a single-threaded limit-order submission microbenchmark: 1M orders after a 10k-order warmup, prices clustered tightly around a mid so most orders rest rather than cross repeatedly. It reports submission latency percentiles and aggregate throughput.

`bench_mixed` models a more realistic order flow instead of pure insertion: after the same 10k-order warmup, it runs 1M operations split 60% limit submits (50/50 buy/sell, price clustered around a mid), 25% cancels (of a randomly chosen currently-resting order), and 15% modifies (price nudged ±1-3 ticks with a fresh random quantity, exercising the loses-time-priority reprice path). Latency is reported separately per operation type — a cancel uses an average O(1) hash lookup and intrusive-list unlink while a submit may sort and walk price levels, so averaging them together would hide that difference — plus one aggregate ops/sec throughput number across all three types combined. Both the workload mix and the RNG seed are fixed, so a given build reproduces the same sequence of operations run to run.

Benchmark results depend on the processor, compiler, optimisation flags, operating system, and current system load. Performance figures should therefore be published together with the complete test environment and methodology.

See [ROADMAP.md](ROADMAP.md) for further planned improvements such as peak-memory tracking and richer hardware profiling.

## Benchmark measurements

### Reproduce locally

Use Bash on Linux or Ubuntu under WSL. Record WSL/VM results as such.
Install Git, CMake 3.16 or later, Make, GCC 12 and Clang 15 before running.
CMake downloads Catch2 v3.7.1 for the correctness tests.

To reproduce the merged CSV-export revision in a fresh checkout:

```bash
git clone https://github.com/aariiparekh3012-collab/limit-order-book-engine.git
cd limit-order-book-engine
git checkout --detach fca08dd2b331dea88fc5c7890b0a9c8bd0f05952
```

For a different revision, use a clean checkout of that revision and record
its full SHA. Run the following from the repository root. Use fresh
`build-local-gcc` and `build-local-clang` directories if an existing CMake
cache was configured with different compiler settings.

```bash
set -euo pipefail
RESULTS_DIR="$(mktemp -d "$PWD/benchmark-local-XXXXXXXX")"

{
  date -u
  git rev-parse HEAD
  git status --short
  uname -a
  cat /etc/os-release
  lscpu
  free -h
  cmake --version
} > "$RESULTS_DIR/environment.txt"

for compiler in gcc clang; do
  if [ "$compiler" = gcc ]; then
    cxx=g++-12
  else
    cxx=clang++-15
  fi
  build_dir="build-local-$compiler"
  "$cxx" --version > "$RESULTS_DIR/compiler-$compiler.txt"

  cmake -S . -B "$build_dir" \
    -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER="$cxx" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DLOB_BUILD_TESTS=ON
  cmake --build "$build_dir" --parallel

  ctest --test-dir "$build_dir" --output-on-failure \
    > "$RESULTS_DIR/tests-$compiler.txt" 2>&1
  cat "$RESULTS_DIR/tests-$compiler.txt"

  cp "$build_dir/CMakeCache.txt" "$RESULTS_DIR/CMakeCache-$compiler.txt"
  cp "$build_dir/compile_commands.json" \
    "$RESULTS_DIR/compile_commands-$compiler.json"

  for run in 1 2 3 4 5; do
    "./$build_dir/bench_submit" --csv \
      > "$RESULTS_DIR/submit-$compiler-$run.csv"
    "./$build_dir/bench_mixed" --csv \
      > "$RESULTS_DIR/mixed-$compiler-$run.csv"
  done
done

printf 'Results saved to: %s\n' "$RESULTS_DIR"
```

The commands build and test each compiler separately, then run both
benchmarks five times sequentially. Five repetitions are the local
reporting protocol; CI currently runs each benchmark once per Release job.
If tests fail, inspect the saved test log before collecting measurements.

Run while plugged in with low background activity. Record the actual power
mode, CPU affinity, tuning and system load. Keep the raw CSVs and environment,
compiler, build and test records together.

### CSV fields and timing scopes

| Fields | Meaning |
|---|---|
| `schema_version`, `benchmark`, `operation` | Schema version 1 and row identity |
| `samples`, `warmup_operations`, `random_seed` | Actual sample count and workload parameters |
| `p50_ns`, `p90_ns`, `p99_ns`, `p999_ns` | Per-operation latency percentiles in nanoseconds |
| `max_ns`, `mean_ns` | Maximum and mean latency in nanoseconds |
| `throughput_ops_per_second` | Throughput, with the row-specific timing scope below |
| `wall_seconds` | Mixed measured-loop duration |
| `final_resting_orders` | Final mixed-workload book size |

Empty CSV fields mean not applicable, not zero.

- `submit / submit`: 1,000,000 measured submissions after 10,000 warmup
  submissions; seed 42. Throughput is `1e9 / mean_ns`; wall time is absent.
- `mixed / submit`, `mixed / cancel`, `mixed / modify`: per-operation
  latencies and actual counts. Selection weights are 60%/25%/15%; actual
  counts need not match these percentages exactly. An empty live pool
  forces a submission.
- `mixed / all`: 1,000,000 measured operations after 10,000 warmup
  operations; seed 1234. Throughput is `1,000,000 / wall_seconds`.
  The measured loop includes workload generation, tracking and bookkeeping.
  This row has no latency percentiles.

Submission throughput and mixed throughput measure different timing scopes.
The fixed seed reproduces a workload within a given build; record the C++
standard library as well when comparing toolchains.

### Local results — pending measurement

No local performance measurements are published yet. Replace the bracketed
entries only after running the commands on the machine described below.
Duplicate the environment and result tables for each measured compiler.
Retain raw data and add a working link before publishing figures.

| Environment | Recorded value |
|---|---|
| Measurement date | [UTC date/time] |
| Commit and working-tree state | [full SHA; clean or describe changes] |
| CPU | [model; physical cores / logical CPUs] |
| RAM | [capacity] |
| OS and kernel | [distribution/version; kernel] |
| Execution environment | [native Linux / WSL version / VM details] |
| Compiler and C++ standard library | [names and full versions] |
| Build | Release; C++17 |
| Effective compile flags | [from compile_commands.json] |
| CMake version | [version] |
| Power mode, affinity, tuning and system load | [actual conditions] |
| Correctness tests | [ctest result] |
| Raw data | [link to CSV files and environment/build/test records] |

Reporting convention: median of each metric across five runs; percentiles
are medians of per-run percentiles, not pooled percentiles. The maximum
column is the largest observed per-run maximum. Preserve all runs.

| Benchmark / operation | Samples per run | p50 ns | p90 ns | p99 ns | p99.9 ns | Mean ns | Largest max ns |
|---|---:|---:|---:|---:|---:|---:|---:|
| Submission / submit | 1,000,000 | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Mixed / submit | [actual count or range] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Mixed / cancel | [actual count or range] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |
| Mixed / modify | [actual count or range] | [ ] | [ ] | [ ] | [ ] | [ ] | [ ] |

| Throughput metric | Median ops/s | Min–max ops/s |
|---|---:|---:|
| Submission: reciprocal of mean measured submission latency | [ ] | [ ] |
| Mixed: full measured-loop wall-clock throughput | [ ] | [ ] |

Mixed wall time: [median; min–max] seconds.
Mixed final resting orders: [actual value or range].

These single-threaded, in-memory microbenchmarks describe the recorded
machine and methodology. They do not measure networked exchange throughput.

### CI artifacts — separate from local measurements

The [CI workflow](.github/workflows/ci.yml) builds GCC/Clang in Debug and
Release on GitHub-hosted `ubuntu-latest` runners. The Release jobs use
`g++-12` and `clang++-15`, run both benchmarks with `--csv`, and upload:

- `benchmark-results-gcc`
- `benchmark-results-clang`

Each artifact contains `benchmark-submit.csv` and `benchmark-mixed.csv`.
Both Release jobs passed and uploaded their artifacts in the
[verified PR #4 run](https://github.com/aariiparekh3012-collab/limit-order-book-engine/actions/runs/39780685988)
on 22 October 2026. Artifact downloads are subject to GitHub retention.

These are CI-runner measurements, not measurements of the local machine.
Passing CI confirms successful execution; the workflow does not enforce a
performance threshold. CI figures are excluded from the local result tables.

## Correctness

Performance is evaluated only alongside correctness. a growing Catch2 test suite across `test_matching` and `test_snapshot` cover matching behaviour such as:

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

- Expanded snapshot-test integration
- Improved replay tooling
- Additional invariants and property-based tests
- Memory-allocation analysis
- Performance profiling and optimisation

## License

This project is licensed under the [MIT License](LICENSE).
