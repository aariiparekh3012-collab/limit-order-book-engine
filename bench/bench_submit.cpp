#include "lob/book.h"
#include "benchmark_csv.h"

#include <chrono>
#include <cstdint>
#include <random>
#include <vector>
#include <iostream>
#include <iomanip>
#include <string>

class NullSink : public lob::EventSink {
public:
    void on_event(const lob::Event&) override {}
};

int main(int argc, char* argv[]) {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono;

    constexpr int WARMUP   = 10'000;
    constexpr int MEASURED = 1'000'000;
    constexpr std::uint32_t RANDOM_SEED = 42;

    bool csv = false;
    if (argc == 2 && std::string(argv[1]) == "--csv") {
        csv = true;
    } else if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        std::cout << "Usage: " << argv[0] << " [--csv]\n";
        return 0;
    } else if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << " [--csv]\n";
        return 2;
    }

    std::mt19937 rng(RANDOM_SEED);
    std::uniform_int_distribution<lob::Price> price_dist(990, 1010);
    std::uniform_int_distribution<lob::Qty>   qty_dist(1, 100);
    std::bernoulli_distribution               side_dist(0.5);

    lob::Book book;
    NullSink sink;

    // generate upfront so allocation doesn't affect timing
    struct TestOrder { lob::Order order; };
    std::vector<TestOrder> orders;
    orders.reserve(WARMUP + MEASURED);

    for (int i = 0; i < WARMUP + MEASURED; ++i) {
        lob::Order o;
        o.id    = static_cast<lob::OrderId>(i + 1);
        o.side  = side_dist(rng) ? lob::Side::Buy : lob::Side::Sell;
        o.price = price_dist(rng);
        o.qty   = qty_dist(rng);
        o.ts    = static_cast<lob::Timestamp>(i + 1);
        orders.push_back({o});
    }

    for (int i = 0; i < WARMUP; ++i)
        book.submit(orders[i].order, sink);

    std::vector<int64_t> latencies;
    latencies.reserve(MEASURED);

    for (int i = WARMUP; i < WARMUP + MEASURED; ++i) {
        auto t0 = Clock::now();
        book.submit(orders[i].order, sink);
        auto t1 = Clock::now();
        latencies.push_back(duration_cast<nanoseconds>(t1 - t0).count());
    }

    const auto stats = lob_bench::summarize(latencies);
    const double throughput = 1e9 / stats.mean_ns;

    if (csv) {
        lob_bench::write_csv_header(std::cout);
        lob_bench::write_csv_row(
            std::cout, "submit", "submit", stats.samples, WARMUP,
            stats, throughput, std::nullopt, RANDOM_SEED);
        return 0;
    }

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "submit benchmark (" << MEASURED << " orders, " << WARMUP << " warmup)\n";
    std::cout << "  p50:   " << stats.p50_ns  << " ns\n";
    std::cout << "  p90:   " << stats.p90_ns  << " ns\n";
    std::cout << "  p99:   " << stats.p99_ns  << " ns\n";
    std::cout << "  p99.9: " << stats.p999_ns << " ns\n";
    std::cout << "  max:   " << stats.max_ns << " ns\n";
    std::cout << "  avg:   " << static_cast<int64_t>(stats.mean_ns) << " ns\n";
    std::cout << std::setprecision(0);
    std::cout << "  throughput: " << throughput << " orders/sec\n";

    return 0;
}
