#include "lob/book.h"
#include "benchmark_csv.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr int WARMUP = 10'000;
constexpr int MEASURED = 1'000'000;
constexpr std::uint32_t RANDOM_SEED = 42;

class NullSink : public lob::EventSink {
public:
    void on_event(const lob::Event&) override {}
};

std::vector<lob::Order> generate_orders() {
    std::mt19937 rng(RANDOM_SEED); // NOSONAR — deterministic benchmark input, not security-sensitive
    std::uniform_int_distribution<lob::Price> price_dist(990, 1010);
    std::uniform_int_distribution<lob::Qty> qty_dist(1, 100);
    std::bernoulli_distribution side_dist(0.5);

    std::vector<lob::Order> orders;
    orders.reserve(WARMUP + MEASURED);
    for (int i = 0; i < WARMUP + MEASURED; ++i) {
        lob::Order order;
        order.id = static_cast<lob::OrderId>(i + 1);
        order.side = side_dist(rng) ? lob::Side::Buy : lob::Side::Sell;
        order.price = price_dist(rng);
        order.qty = qty_dist(rng);
        order.ts = static_cast<lob::Timestamp>(i + 1);
        orders.push_back(order);
    }
    return orders;
}

lob_bench::LatencySummary run_benchmark() {
    auto orders = generate_orders();
    lob::Book book;
    NullSink sink;

    for (int i = 0; i < WARMUP; ++i) book.submit(orders[i], sink);

    std::vector<std::int64_t> latencies;
    latencies.reserve(MEASURED);
    for (int i = WARMUP; i < WARMUP + MEASURED; ++i) {
        const auto start = Clock::now();
        book.submit(orders[i], sink);
        const auto end = Clock::now();
        latencies.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    return lob_bench::summarize(latencies);
}

void report_csv(const lob_bench::LatencySummary& stats) {
    lob_bench::write_csv_header(std::cout);
    lob_bench::write_csv_row(std::cout, {
        "submit", "submit", stats.samples, WARMUP, stats,
        1e9 / stats.mean_ns, std::nullopt, RANDOM_SEED, std::nullopt});
}

void report_human(const lob_bench::LatencySummary& stats) {
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "submit benchmark (" << MEASURED << " orders, " << WARMUP << " warmup)\n";
    std::cout << "  p50:   " << stats.p50_ns << " ns\n";
    std::cout << "  p90:   " << stats.p90_ns << " ns\n";
    std::cout << "  p99:   " << stats.p99_ns << " ns\n";
    std::cout << "  p99.9: " << stats.p999_ns << " ns\n";
    std::cout << "  max:   " << stats.max_ns << " ns\n";
    std::cout << "  avg:   " << static_cast<std::int64_t>(stats.mean_ns) << " ns\n";
    std::cout << std::setprecision(0);
    std::cout << "  throughput: " << (1e9 / stats.mean_ns) << " orders/sec\n";
}

} // namespace

int main(int argc, char* argv[]) {
    const auto options = lob_bench::parse_options(argc, argv, std::cout, std::cerr);
    if (!options.should_run) return options.exit_code;

    const auto stats = run_benchmark();
    if (options.format == lob_bench::OutputFormat::Csv) report_csv(stats);
    else report_human(stats);
    return 0;
}
