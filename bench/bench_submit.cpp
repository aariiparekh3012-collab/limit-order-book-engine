#include "lob/book.h"

#include <chrono>
#include <random>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <numeric>

class NullSink : public lob::EventSink {
public:
    void on_event(const lob::Event&) override {}
};

int main() {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono;

    constexpr int WARMUP   = 10'000;
    constexpr int MEASURED = 1'000'000;

    std::mt19937 rng(42);
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

    std::sort(latencies.begin(), latencies.end());

    auto pct = [&](double p) -> int64_t {
        auto idx = static_cast<size_t>(p * latencies.size());
        return latencies[std::min(idx, latencies.size() - 1)];
    };

    int64_t total = std::accumulate(latencies.begin(), latencies.end(), int64_t{0});
    double avg = static_cast<double>(total) / MEASURED;

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "submit benchmark (" << MEASURED << " orders, " << WARMUP << " warmup)\n";
    std::cout << "  p50:   " << pct(0.50)  << " ns\n";
    std::cout << "  p90:   " << pct(0.90)  << " ns\n";
    std::cout << "  p99:   " << pct(0.99)  << " ns\n";
    std::cout << "  p99.9: " << pct(0.999) << " ns\n";
    std::cout << "  max:   " << latencies.back() << " ns\n";
    std::cout << "  avg:   " << static_cast<int64_t>(avg) << " ns\n";
    std::cout << std::setprecision(0);
    std::cout << "  throughput: " << (1e9 / avg) << " orders/sec\n";

    return 0;
}
