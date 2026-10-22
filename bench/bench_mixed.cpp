// Mixed-workload benchmark: 60% submits, 25% cancels, and 15% modifies.
// A 10k-operation warmup precedes 1M measured operations.

#include "lob/book.h"
#include "benchmark_csv.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <random>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

using namespace lob;

namespace {

using Clock = std::chrono::steady_clock;

constexpr int WARMUP = 10'000;
constexpr int MEASURED = 1'000'000;
constexpr Price MID = 1000;
constexpr std::uint32_t RANDOM_SEED = 1234;

class NullSink : public EventSink {
public:
    void on_event(const Event&) override {}
};

class TrackingSink : public EventSink {
public:
    OrderId watched_id = 0;
    bool watched_filled = false;
    std::vector<OrderId> other_filled;

    void reset(OrderId id) {
        watched_id = id;
        watched_filled = false;
        other_filled.clear();
    }

    void on_event(const Event& event) override {
        if (const auto* fill = std::get_if<Filled>(&event)) {
            if (fill->order_id == watched_id) watched_filled = true;
            else other_filled.push_back(fill->order_id);
        }
    }
};

class LivePool {
public:
    void add(OrderId id, Price price) {
        index_[id] = ids_.size();
        ids_.push_back(id);
        price_[id] = price;
    }

    void remove(OrderId id) {
        const auto found = index_.find(id);
        if (found == index_.end()) return;
        const std::size_t index = found->second;
        const OrderId last = ids_.back();
        ids_[index] = last;
        index_[last] = index;
        ids_.pop_back();
        index_.erase(found);
        price_.erase(id);
    }

    void set_price(OrderId id, Price price) { price_[id] = price; }
    Price price_of(OrderId id) const { return price_.at(id); }
    bool empty() const { return ids_.empty(); }

    template<typename Rng>
    OrderId random_id(Rng& rng) const {
        std::uniform_int_distribution<std::size_t> distribution(0, ids_.size() - 1);
        return ids_[distribution(rng)];
    }

private:
    std::vector<OrderId> ids_;
    std::unordered_map<OrderId, std::size_t> index_;
    std::unordered_map<OrderId, Price> price_;
};

enum class OpType { Submit, Cancel, Modify };

struct MixedResult {
    lob_bench::LatencySummary submit;
    lob_bench::LatencySummary cancel;
    lob_bench::LatencySummary modify;
    double wall_seconds = 0.0;
    std::size_t final_resting_orders = 0;
};

class MixedBenchmark {
public:
    MixedResult run() {
        for (int i = 0; i < WARMUP; ++i) run_one();

        std::vector<std::int64_t> submit_latencies;
        std::vector<std::int64_t> cancel_latencies;
        std::vector<std::int64_t> modify_latencies;
        submit_latencies.reserve(MEASURED * 60 / 100);
        cancel_latencies.reserve(MEASURED * 25 / 100);
        modify_latencies.reserve(MEASURED * 15 / 100);

        const auto start = Clock::now();
        for (int i = 0; i < MEASURED; ++i) {
            const auto [type, latency] = run_one();
            if (type == OpType::Submit) submit_latencies.push_back(latency);
            else if (type == OpType::Cancel) cancel_latencies.push_back(latency);
            else modify_latencies.push_back(latency);
        }
        const auto end = Clock::now();

        return {
            lob_bench::summarize(submit_latencies),
            lob_bench::summarize(cancel_latencies),
            lob_bench::summarize(modify_latencies),
            std::chrono::duration_cast<std::chrono::duration<double>>(end - start).count(),
            book_.order_count()};
    }

private:
    std::int64_t submit() {
        Order order;
        order.id = next_id_++;
        order.side = side_dist_(rng_) ? Side::Buy : Side::Sell;
        order.price = MID + price_jitter_(rng_);
        if (order.price <= 0) order.price = MID;
        order.qty = qty_dist_(rng_);
        order.ts = next_ts_++;
        order.type = OrderType::Limit;

        tracking_sink_.reset(order.id);
        const auto start = Clock::now();
        book_.submit(order, tracking_sink_);
        const auto end = Clock::now();

        for (const auto id : tracking_sink_.other_filled) live_.remove(id);
        if (!tracking_sink_.watched_filled) live_.add(order.id, order.price);
        return elapsed_ns(start, end);
    }

    std::int64_t cancel() {
        const OrderId id = live_.random_id(rng_);
        const auto start = Clock::now();
        book_.cancel(id, null_sink_);
        const auto end = Clock::now();
        live_.remove(id);
        return elapsed_ns(start, end);
    }

    std::int64_t modify() {
        const OrderId id = live_.random_id(rng_);
        const Price old_price = live_.price_of(id);
        const int delta = tick_delta_(rng_) * (tick_sign_(rng_) ? 1 : -1);
        Price new_price = old_price + delta;
        if (new_price <= 0) new_price = old_price + std::abs(delta);
        const Qty new_qty = qty_dist_(rng_);

        const auto start = Clock::now();
        book_.modify(id, new_price, new_qty, null_sink_);
        const auto end = Clock::now();
        live_.set_price(id, new_price);
        return elapsed_ns(start, end);
    }

    std::pair<OpType, std::int64_t> run_one() {
        OpType type = static_cast<OpType>(op_dist_(rng_));
        if (live_.empty() && type != OpType::Submit) type = OpType::Submit;

        switch (type) {
            case OpType::Submit: return {type, submit()};
            case OpType::Cancel: return {type, cancel()};
            case OpType::Modify: return {type, modify()};
        }
        return {OpType::Submit, 0};
    }

    static std::int64_t elapsed_ns(Clock::time_point start, Clock::time_point end) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }

    std::mt19937 rng_{RANDOM_SEED}; // NOSONAR — deterministic benchmark input, not security-sensitive
    std::uniform_int_distribution<Price> price_jitter_{-10, 10};
    std::uniform_int_distribution<Qty> qty_dist_{1, 100};
    std::bernoulli_distribution side_dist_{0.5};
    std::uniform_int_distribution<int> tick_delta_{1, 3};
    std::bernoulli_distribution tick_sign_{0.5};
    std::discrete_distribution<int> op_dist_{0.60, 0.25, 0.15};
    Book book_;
    NullSink null_sink_;
    TrackingSink tracking_sink_;
    LivePool live_;
    OrderId next_id_ = 1;
    Timestamp next_ts_ = 1;
};

void report_latency(const char* label, const lob_bench::LatencySummary& stats) {
    std::cout << "  " << label << " (n=" << stats.samples << ")\n";
    std::cout << "    p50: " << stats.p50_ns << " ns   "
              << "p90: " << stats.p90_ns << " ns   "
              << "p99: " << stats.p99_ns << " ns   "
              << "p99.9: " << stats.p999_ns << " ns   "
              << "max: " << stats.max_ns << " ns   "
              << "avg: " << static_cast<std::int64_t>(stats.mean_ns) << " ns\n";
}

void report_csv(const MixedResult& result) {
    lob_bench::write_csv_header(std::cout);
    lob_bench::write_csv_row(std::cout, {
        "mixed", "submit", result.submit.samples, WARMUP, result.submit,
        std::nullopt, std::nullopt, RANDOM_SEED, std::nullopt});
    lob_bench::write_csv_row(std::cout, {
        "mixed", "cancel", result.cancel.samples, WARMUP, result.cancel,
        std::nullopt, std::nullopt, RANDOM_SEED, std::nullopt});
    lob_bench::write_csv_row(std::cout, {
        "mixed", "modify", result.modify.samples, WARMUP, result.modify,
        std::nullopt, std::nullopt, RANDOM_SEED, std::nullopt});
    lob_bench::write_csv_row(std::cout, {
        "mixed", "all", MEASURED, WARMUP, std::nullopt,
        MEASURED / result.wall_seconds, result.wall_seconds,
        RANDOM_SEED, result.final_resting_orders});
}

void report_human(const MixedResult& result) {
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "mixed workload benchmark (" << MEASURED << " ops, " << WARMUP << " warmup)\n";
    std::cout << "  mix: submit=" << result.submit.samples
              << " (" << (100.0 * result.submit.samples / MEASURED) << "%)"
              << "  cancel=" << result.cancel.samples
              << " (" << (100.0 * result.cancel.samples / MEASURED) << "%)"
              << "  modify=" << result.modify.samples
              << " (" << (100.0 * result.modify.samples / MEASURED) << "%)\n";
    std::cout << "  final resting order count: " << result.final_resting_orders << "\n\n";
    report_latency("SUBMIT", result.submit);
    report_latency("CANCEL", result.cancel);
    report_latency("MODIFY", result.modify);
    std::cout << "\n  wall time:  " << result.wall_seconds << " s\n";
    std::cout << std::setprecision(0);
    std::cout << "  aggregate throughput: "
              << (MEASURED / result.wall_seconds) << " ops/sec\n";
}

} // namespace

int main(int argc, char* argv[]) {
    const auto options = lob_bench::parse_options(argc, argv, std::cout, std::cerr);
    if (!options.should_run) return options.exit_code;

    const auto result = MixedBenchmark{}.run();
    if (options.format == lob_bench::OutputFormat::Csv) report_csv(result);
    else report_human(result);
    return 0;
}
