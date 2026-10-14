#include "lob/cli.h"
#include "lob/json_sink.h"
#include "lob/log_sanitize.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct CliResult {
    int code;
    std::string out;
    std::string err;
};

CliResult run(std::vector<std::string> args, const std::string& input = {}) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& arg : args) argv.push_back(arg.data());

    std::istringstream in(input);
    std::ostringstream out;
    std::ostringstream err;
    int code = lob::cli::run_cli(static_cast<int>(argv.size()), argv.data(), in, out, err);
    return {code, out.str(), err.str()};
}

} // namespace

TEST_CASE("CLI validates arguments without throwing", "[cli]") {
    auto no_args = run({"lob_cli"});
    REQUIRE(no_args.code == 1);
    REQUIRE(no_args.err.find("usage:") != std::string::npos);

    auto missing_stp = run({"lob_cli", "--stp"});
    REQUIRE(missing_stp.code == 1);
    REQUIRE(missing_stp.err.find("needs an argument") != std::string::npos);

    auto bad_stp = run({"lob_cli", "--stp", "sideways", "-"});
    REQUIRE(bad_stp.code == 1);
    REQUIRE(bad_stp.err.find("unknown STP mode") != std::string::npos);

    auto missing_depth = run({"lob_cli", "--depth"});
    REQUIRE(missing_depth.code == 1);

    auto bad_depth = run({"lob_cli", "--depth", "many", "-"});
    REQUIRE(bad_depth.code == 1);
    REQUIRE(bad_depth.err.find("non-negative integer") != std::string::npos);

    auto negative_depth = run({"lob_cli", "--depth", "-2", "-"});
    REQUIRE(negative_depth.code == 1);

    auto no_path = run({"lob_cli", "--json"});
    REQUIRE(no_path.code == 1);
    REQUIRE(no_path.err.find("usage:") != std::string::npos);
}

TEST_CASE("CLI processes plain-text submit, modify, trade, cancel, and depth", "[cli]") {
    const std::string csv =
        "# comment\n"
        "id,symbol,side,type,price,qty,trader_id\n"
        "1,ABC,b,l,100,10,1\n"
        "MODIFY,1,101,12\n"
        "2,ABC,sell,limit,102,5,2\n"
        "3,ABC,S,MKT,0,20,3\n"
        "CANCEL,2\n"
        "CANCEL,999\n";

    auto result = run({"lob_cli", "--depth", "3", "-"}, csv);
    REQUIRE(result.code == 0);
    REQUIRE(result.err.empty());
    REQUIRE(result.out.find("-- new symbol: ABC --") != std::string::npos);
    REQUIRE(result.out.find("MODIFY_ACK id=1") != std::string::npos);
    REQUIRE(result.out.find("TRADE") != std::string::npos);
    REQUIRE(result.out.find("PARTIAL") != std::string::npos);
    REQUIRE(result.out.find("CANCEL_ACK id=2") != std::string::npos);
    REQUIRE(result.out.find("REJECT     id=999") != std::string::npos);
    REQUIRE(result.out.find("depth [ABC]") != std::string::npos);
    REQUIRE(result.out.find("BID ---") != std::string::npos);
    REQUIRE(result.out.find("ASK ---") != std::string::npos);
}

TEST_CASE("CLI emits JSON for every event family", "[cli][json]") {
    const std::string csv =
        "1,ABC,BUY,LIMIT,100,5,7\n"
        "2,ABC,SELL,LIMIT,100,2,8\n"
        "MODIFY,1,101,4\n"
        "3,ABC,SELL,IOC,101,6,9\n"
        "CANCEL,1\n"
        "CANCEL,404\n";

    auto result = run({"lob_cli", "--json", "-"}, csv);
    REQUIRE(result.code == 0);
    REQUIRE(result.err.empty());
    REQUIRE(result.out.find("\"event\":\"ACK\"") != std::string::npos);
    REQUIRE(result.out.find("\"event\":\"TRADE\"") != std::string::npos);
    REQUIRE(result.out.find("\"event\":\"FILLED\"") != std::string::npos);
    REQUIRE(result.out.find("\"event\":\"PARTIAL\"") != std::string::npos);
    REQUIRE(result.out.find("\"event\":\"MODIFY_ACK\"") != std::string::npos);
    REQUIRE(result.out.find("\"event\":\"CANCEL_ACK\"") != std::string::npos);
    REQUIRE(result.out.find("\"event\":\"REJECT\"") != std::string::npos);
}

TEST_CASE("CLI supports all STP modes", "[cli][stp]") {
    const std::string self_cross =
        "1,ABC,SELL,LIMIT,100,5,42\n"
        "2,ABC,BUY,LIMIT,100,5,42\n";

    auto newest = run({"lob_cli", "--stp", "newest", "-"}, self_cross);
    REQUIRE(newest.code == 0);
    REQUIRE(newest.out.find("CANCEL_NEWEST") != std::string::npos);

    auto newest_alias = run({"lob_cli", "--stp", "cancel_newest", "-"}, self_cross);
    REQUIRE(newest_alias.code == 0);
    REQUIRE(newest_alias.out.find("CANCEL_NEWEST") != std::string::npos);

    auto oldest = run({"lob_cli", "--stp", "cancel_oldest", "-"}, self_cross);
    REQUIRE(oldest.code == 0);
    REQUIRE(oldest.out.find("CANCEL_OLDEST") != std::string::npos);

    auto both = run({"lob_cli", "--stp", "both", "-"}, self_cross);
    REQUIRE(both.code == 0);
    REQUIRE(both.out.find("CANCEL_BOTH") != std::string::npos);

    auto both_alias = run({"lob_cli", "--stp", "cancel_both", "-"}, self_cross);
    REQUIRE(both_alias.code == 0);
    REQUIRE(both_alias.out.find("CANCEL_BOTH") != std::string::npos);

    auto oldest_alias = run({"lob_cli", "--stp", "oldest", "-"}, self_cross);
    REQUIRE(oldest_alias.code == 0);
    REQUIRE(oldest_alias.out.find("CANCEL_OLDEST") != std::string::npos);

    auto json = run({"lob_cli", "--json", "--stp", "newest", "-"}, self_cross);
    REQUIRE(json.code == 0);
    REQUIRE(json.out.find("\"event\":\"STP_CANCEL\"") != std::string::npos);
}

TEST_CASE("CLI accepts compact order aliases and an empty trader field", "[cli]") {
    const std::string csv =
        "   \n"
        "1,ABC,BUY,LMT,100,1,\n"
        "2,ABC,SELL,M,0,1\n"
        "3,DEF,BUY,FOK,100,1\n"
        "4,GHI,BUY,LIMIT,100,1\n"
        "5,GHI,SELL,MARKET,0,1\n";

    auto result = run({"lob_cli", "-"}, csv);
    REQUIRE(result.code == 0);
    REQUIRE(result.err.empty());
    REQUIRE(result.out.find("LIMIT BUY ABC id=1") != std::string::npos);
    REQUIRE(result.out.find("MARKET SELL ABC id=2") != std::string::npos);
    REQUIRE(result.out.find("FOK BUY DEF id=3") != std::string::npos);
    REQUIRE(result.out.find("MARKET SELL GHI id=5") != std::string::npos);
}

TEST_CASE("CLI reports malformed rows and sanitizes control characters", "[cli]") {
    const std::string csv =
        "CANCEL\n"
        "CANCEL,not-a-number\n"
        "MODIFY,1,2\n"
        "MODIFY,not-a-number,2,3\n"
        "1,ABC\n"
        "2,ABC,sideways,LIMIT,100,1\n"
        "3,ABC,BUY,UNKNOWN,100,1\n"
        "4,ABC,BUY,LIMIT,not-a-price,1\n"
        "5,BAD\tSYMBOL,BUY,LIMIT,100,1\n";

    auto result = run({"lob_cli", "-"}, csv);
    REQUIRE(result.code == 0);
    REQUIRE(result.err.find("cancel needs id") != std::string::npos);
    REQUIRE(result.err.find("modify needs id,new_price,new_qty") != std::string::npos);
    REQUIRE(result.err.find("need 6 fields") != std::string::npos);
    REQUIRE(result.err.find("bad side") != std::string::npos);
    REQUIRE(result.err.find("bad type") != std::string::npos);
    REQUIRE(result.out.find("BAD SYMBOL") != std::string::npos);
    REQUIRE(result.out.find('\t') == std::string::npos);
}

TEST_CASE("CLI reads a named file and reports a missing file", "[cli][file]") {
    const auto path = std::filesystem::temp_directory_path() / "lob_cli_test_orders.csv";
    {
        std::ofstream file(path);
        file << "1,ABC,BUY,LIMIT,100,1\n";
    }

    auto from_file = run({"lob_cli", path.string()});
    REQUIRE(from_file.code == 0);
    REQUIRE(from_file.out.find("ACK        id=1") != std::string::npos);
    std::filesystem::remove(path);

    auto missing = run({"lob_cli", path.string()});
    REQUIRE(missing.code == 1);
    REQUIRE(missing.err.find("can't open") != std::string::npos);
}

TEST_CASE("JSON and plain-text logging escape untrusted text", "[cli][logging]") {
    std::ostringstream json;
    lob::JsonSink sink(json);
    sink.on_event(lob::Reject{7, "quote\" slash\\ line\nreturn\rtab\t"});

    REQUIRE(json.str() ==
            "{\"event\":\"REJECT\",\"order_id\":7,\"reason\":"
            "\"quote\\\" slash\\\\ line\\nreturn\\rtab\\t\"}\n");

    const std::string unsafe{'A', '\n', 'B', '\r', 'C', '\t', 'D', '\0',
                             'E', '\x01', 'F', '\x7f', 'G'};
    REQUIRE(lob::sanitize_for_log(unsafe) == "A B C D E F G");
}
