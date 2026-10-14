#pragma once

#include <iosfwd>

namespace lob::cli {

int run_cli(int argc, char* argv[], std::istream& in, std::ostream& out,
            std::ostream& err);

} // namespace lob::cli
