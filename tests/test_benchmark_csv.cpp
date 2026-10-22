#include "benchmark_csv.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <vector>

TEST_CASE("benchmark latency summary preserves existing percentile semantics") {
    const std::vector<std::int64_t> samples{9, 1, 7, 3, 5};

    const auto stats = lob_bench::summarize(samples);

    REQUIRE(stats.samples == 5);
    REQUIRE(stats.p50_ns == 5);
    REQUIRE(stats.p90_ns == 9);
    REQUIRE(stats.p99_ns == 9);
    REQUIRE(stats.p999_ns == 9);
    REQUIRE(stats.max_ns == 9);
    REQUIRE(stats.mean_ns == 5.0);
}

TEST_CASE("benchmark CSV has a stable header and escapes text fields") {
    std::ostringstream output;
    const lob_bench::LatencySummary stats{2, 10, 20, 30, 40, 50, 25.5};

    lob_bench::write_csv_header(output);
    lob_bench::write_csv_row(output, {
        "mixed,workload", "sub\"mit", 2, 10, stats,
        123.25, 0.5, 42, 7});

    REQUIRE(output.str() ==
        "schema_version,benchmark,operation,samples,warmup_operations,"
        "p50_ns,p90_ns,p99_ns,p999_ns,max_ns,mean_ns,"
        "throughput_ops_per_second,wall_seconds,random_seed,final_resting_orders\n"
        "1,\"mixed,workload\",\"sub\"\"mit\",2,10,10,20,30,40,50,25.500,"
        "123.250,0.500000000,42,7\n");
}
