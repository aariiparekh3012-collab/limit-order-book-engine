// mixed-workload benchmark: submits, cancels, and modifies interleaved,
// approximating a realistic market-making / retail order flow instead of
// bench_submit's pure-insertion workload.
//
// workload mix (by operation count):
//   60% limit submits  — 50/50 buy/sell, price clustered around a mid
//   25% cancels        — of a randomly chosen currently-resting order
//   15% modifies        — price +/- 1..3 ticks, fresh random qty
//
// 10k warmup ops (untimed) followed by 1M measured ops. latency is reported
// separately per operation type (each op type only competes against the
// same book state a real gateway would see), plus one aggregate throughput
// number across all operation types combined.

#include "lob/book.h"

#include <chrono>
#include <random>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <variant>

using namespace lob;

namespace {

class NullSink : public EventSink {
public:
    void on_event(const Event&) override {}
};

// tracks which submitted order ended up filled, and which resting
// counterparty orders got filled as a side effect, so the benchmark driver
// can keep its "currently resting" order-id pool accurate without paying
// for a full VectorSink allocation on every call.
class TrackingSink : public EventSink {
public:
    OrderId watched_id      = 0;
    bool    watched_filled  = false;
    std::vector<OrderId> other_filled;

    void reset(OrderId id) {
        watched_id = id;
        watched_filled = false;
        other_filled.clear();
    }

    void on_event(const Event& e) override {
        if (auto* f = std::get_if<Filled>(&e)) {
            if (f->order_id == watched_id) watched_filled = true;
            else other_filled.push_back(f->order_id);
        }
    }
};

// O(1) add/remove/pick-random pool of currently-resting order ids, with
// their last-known price (needed so "modify" can nudge relative to the
// order's own price rather than an arbitrary one).
class LivePool {
public:
    void add(OrderId id, Price price) {
        index_[id] = ids_.size();
        ids_.push_back(id);
        price_[id] = price;
    }

    void remove(OrderId id) {
        auto it = index_.find(id);
        if (it == index_.end()) return; // already gone (e.g. filled twice in one call)
        size_t idx = it->second;
        OrderId last = ids_.back();
        ids_[idx] = last;
        index_[last] = idx;
        ids_.pop_back();
        index_.erase(it);
        price_.erase(id);
    }

    void set_price(OrderId id, Price p) { price_[id] = p; }
    Price price_of(OrderId id) const { return price_.at(id); }

    bool empty() const { return ids_.empty(); }
    size_t size() const { return ids_.size(); }

    template<typename Rng>
    OrderId random_id(Rng& rng) const {
        std::uniform_int_distribution<size_t> d(0, ids_.size() - 1);
        return ids_[d(rng)];
    }

private:
    std::vector<OrderId> ids_;
    std::unordered_map<OrderId, size_t> index_;
    std::unordered_map<OrderId, Price>  price_;
};

enum class OpType { Submit, Cancel, Modify };

struct LatencyStats {
    std::vector<int64_t> samples;

    void record(int64_t ns) { samples.push_back(ns); }

    void report(const char* label) const {
        if (samples.empty()) {
            std::cout << "  " << label << ": (no samples)\n";
            return;
        }
        std::vector<int64_t> sorted = samples;
        std::sort(sorted.begin(), sorted.end());

        auto pct = [&](double p) -> int64_t {
            auto idx = static_cast<size_t>(p * sorted.size());
            return sorted[std::min(idx, sorted.size() - 1)];
        };

        int64_t total = std::accumulate(sorted.begin(), sorted.end(), int64_t{0});
        double avg = static_cast<double>(total) / sorted.size();

        std::cout << "  " << label << " (n=" << sorted.size() << ")\n";
        std::cout << "    p50: " << pct(0.50) << " ns   "
                   << "p90: " << pct(0.90) << " ns   "
                   << "p99: " << pct(0.99) << " ns   "
                   << "p99.9: " << pct(0.999) << " ns   "
                   << "max: " << sorted.back() << " ns   "
                   << "avg: " << static_cast<int64_t>(avg) << " ns\n";
    }
};

} // namespace

int main() {
    using Clock = std::chrono::steady_clock;
    using namespace std::chrono;

    constexpr int WARMUP   = 10'000;
    constexpr int MEASURED = 1'000'000;
    constexpr Price MID    = 1000;

    std::mt19937 rng(1234); // NOSONAR — deterministic seed is intentional for reproducible benchmarks
    std::uniform_int_distribution<Price> price_jitter(-10, 10);
    std::uniform_int_distribution<Qty>   qty_dist(1, 100);
    std::bernoulli_distribution          side_dist(0.5);
    std::uniform_int_distribution<int>   tick_delta(1, 3);
    std::bernoulli_distribution          tick_sign(0.5);
    std::discrete_distribution<int>      op_dist({0.60, 0.25, 0.15}); // Submit, Cancel, Modify

    Book book;
    NullSink null_sink;
    TrackingSink tracking_sink;
    LivePool live;

    OrderId next_id = 1;
    Timestamp next_ts = 1;

    auto do_submit = [&](bool timed) -> int64_t {
        Order o;
        o.id    = next_id++;
        o.side  = side_dist(rng) ? Side::Buy : Side::Sell;
        o.price = MID + price_jitter(rng);
        if (o.price <= 0) o.price = MID;
        o.qty   = qty_dist(rng);
        o.ts    = next_ts++;
        o.type  = OrderType::Limit;

        tracking_sink.reset(o.id);

        auto t0 = Clock::now();
        book.submit(o, tracking_sink);
        auto t1 = Clock::now();

        for (auto id : tracking_sink.other_filled) live.remove(id);
        if (!tracking_sink.watched_filled) live.add(o.id, o.price);

        return timed ? duration_cast<nanoseconds>(t1 - t0).count() : 0;
    };

    auto do_cancel = [&](bool timed) -> int64_t {
        OrderId id = live.random_id(rng);

        auto t0 = Clock::now();
        book.cancel(id, null_sink);
        auto t1 = Clock::now();

        live.remove(id);
        return timed ? duration_cast<nanoseconds>(t1 - t0).count() : 0;
    };

    auto do_modify = [&](bool timed) -> int64_t {
        OrderId id = live.random_id(rng);
        Price old_price = live.price_of(id);
        int delta = tick_delta(rng) * (tick_sign(rng) ? 1 : -1);
        Price new_price = old_price + delta;
        if (new_price <= 0) new_price = old_price + std::abs(delta);
        Qty new_qty = qty_dist(rng);

        auto t0 = Clock::now();
        book.modify(id, new_price, new_qty, null_sink);
        auto t1 = Clock::now();

        live.set_price(id, new_price);
        return timed ? duration_cast<nanoseconds>(t1 - t0).count() : 0;
    };

    auto run_one = [&](bool timed) -> std::pair<OpType, int64_t> {
        OpType type = static_cast<OpType>(op_dist(rng));
        if (live.empty() && type != OpType::Submit) type = OpType::Submit;

        switch (type) {
            case OpType::Submit: return {OpType::Submit, do_submit(timed)};
            case OpType::Cancel: return {OpType::Cancel, do_cancel(timed)};
            case OpType::Modify: return {OpType::Modify, do_modify(timed)};
        }
        return {OpType::Submit, 0};
    };

    // warmup — populate the book with a realistic resting-order pool and
    // let the allocator settle before we start timing.
    for (int i = 0; i < WARMUP; ++i) run_one(false);

    LatencyStats submit_lat, cancel_lat, modify_lat;
    int submit_n = 0, cancel_n = 0, modify_n = 0;

    auto wall_start = Clock::now();
    for (int i = 0; i < MEASURED; ++i) {
        auto [type, ns] = run_one(true);
        switch (type) {
            case OpType::Submit: submit_lat.record(ns); ++submit_n; break;
            case OpType::Cancel: cancel_lat.record(ns); ++cancel_n; break;
            case OpType::Modify: modify_lat.record(ns); ++modify_n; break;
        }
    }
    auto wall_end = Clock::now();

    double wall_seconds = duration_cast<duration<double>>(wall_end - wall_start).count();

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "mixed workload benchmark (" << MEASURED << " ops, " << WARMUP << " warmup)\n";
    std::cout << "  mix: submit=" << submit_n << " (" << (100.0 * submit_n / MEASURED) << "%)"
               << "  cancel=" << cancel_n << " (" << (100.0 * cancel_n / MEASURED) << "%)"
               << "  modify=" << modify_n << " (" << (100.0 * modify_n / MEASURED) << "%)\n";
    std::cout << "  final resting order count: " << book.order_count() << "\n\n";

    submit_lat.report("SUBMIT");
    cancel_lat.report("CANCEL");
    modify_lat.report("MODIFY");

    std::cout << "\n  wall time:  " << wall_seconds << " s\n";
    std::cout << std::setprecision(0);
    std::cout << "  aggregate throughput: " << (MEASURED / wall_seconds) << " ops/sec\n";

    return 0;
}
