#pragma once

#include <string>

namespace lob {

inline std::string sanitize_for_log(const std::string& input) {
    std::string out;
    out.reserve(input.size());
    for (unsigned char ch : input) {
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch == '\0') {
            out.push_back(' ');
        } else if (ch < 0x20 || ch == 0x7f) {
            out.push_back(' ');
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

} // namespace lob
