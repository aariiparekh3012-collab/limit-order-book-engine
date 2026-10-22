#pragma once

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <numeric>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

namespace lob_bench {

struct LatencySummary {
    std::size_t samples = 0;
    std::int64_t p50_ns = 0;
    std::int64_t p90_ns = 0;
    std::int64_t p99_ns = 0;
    std::int64_t p999_ns = 0;
    std::int64_t max_ns = 0;
    double mean_ns = 0.0;
};

inline LatencySummary summarize(const std::vector<std::int64_t>& samples) {
    LatencySummary result;
    result.samples = samples.size();
    if (samples.empty()) return result;

    std::vector<std::int64_t> sorted = samples;
    std::sort(sorted.begin(), sorted.end());

    const auto percentile = [&](double p) {
        const auto index = static_cast<std::size_t>(p * sorted.size());
        return sorted[std::min(index, sorted.size() - 1)];
    };

    const auto total = std::accumulate(sorted.begin(), sorted.end(), std::int64_t{0});
    result.p50_ns = percentile(0.50);
    result.p90_ns = percentile(0.90);
    result.p99_ns = percentile(0.99);
    result.p999_ns = percentile(0.999);
    result.max_ns = sorted.back();
    result.mean_ns = static_cast<double>(total) / sorted.size();
    return result;
}

inline void write_csv_field(std::ostream& out, const std::string& value) {
    out << '"';
    for (const char ch : value) {
        if (ch == '"') out << '"';
        out << ch;
    }
    out << '"';
}

inline void write_csv_header(std::ostream& out) {
    out << "schema_version,benchmark,operation,samples,warmup_operations,"
           "p50_ns,p90_ns,p99_ns,p999_ns,max_ns,mean_ns,"
           "throughput_ops_per_second,wall_seconds,random_seed,final_resting_orders\n";
}

inline void write_csv_row(
    std::ostream& out,
    const std::string& benchmark,
    const std::string& operation,
    std::size_t samples,
    std::size_t warmup_operations,
    const std::optional<LatencySummary>& latency,
    const std::optional<double>& throughput,
    const std::optional<double>& wall_seconds,
    std::uint32_t random_seed,
    const std::optional<std::size_t>& final_resting_orders = std::nullopt) {

    out << "1,";
    write_csv_field(out, benchmark);
    out << ',';
    write_csv_field(out, operation);
    out << ',' << samples << ',' << warmup_operations << ',';

    if (latency) {
        out << latency->p50_ns << ',' << latency->p90_ns << ','
            << latency->p99_ns << ',' << latency->p999_ns << ','
            << latency->max_ns << ',' << std::fixed << std::setprecision(3)
            << latency->mean_ns;
    } else {
        out << ",,,,,";
    }

    out << ',';
    if (throughput) out << std::fixed << std::setprecision(3) << *throughput;
    out << ',';
    if (wall_seconds) out << std::fixed << std::setprecision(9) << *wall_seconds;
    out << ',' << random_seed << ',';
    if (final_resting_orders) out << *final_resting_orders;
    out << '\n';
}

} // namespace lob_bench
