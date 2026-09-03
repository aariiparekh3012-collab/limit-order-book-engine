#pragma once

#include "event.h"
#include <iostream>
#include <string>

namespace lob {

// writes each event as a single JSON line to an ostream.
// one object per line, no trailing comma, no wrapping array.
// meant for structured logging, replay, or piping into jq.
class JsonSink : public EventSink {
public:
    explicit JsonSink(std::ostream& out) : out_(out) {}

    void on_event(const Event& event) override {
        std::visit([this](const auto& e) { write(e); }, event);
    }

private:
    std::ostream& out_;

    // helpers
    static std::string escape(const std::string& s) {
        std::string r;
        r.reserve(s.size());
        for (char c : s) {
            switch (c) {
                case '"':  r += "\\\""; break;
                case '\\': r += "\\\\"; break;
                case '\n': r += "\\n";  break;
                case '\r': r += "\\r";  break;
                case '\t': r += "\\t";  break;
                default:   r += c;
            }
        }
        return r;
    }

    void write(const Ack& e) {
        out_ << R"({"event":"ACK","order_id":)" << e.order_id << "}\n";
    }

    void write(const Trade& e) {
        out_ << R"({"event":"TRADE","aggressor_id":)" << e.aggressor_id
             << R"(,"resting_id":)" << e.resting_id
             << R"(,"price":)" << e.price
             << R"(,"qty":)" << e.qty
             << R"(,"ts":)" << e.ts << "}\n";
    }

    void write(const Filled& e) {
        out_ << R"({"event":"FILLED","order_id":)" << e.order_id << "}\n";
    }

    void write(const Partial& e) {
        out_ << R"({"event":"PARTIAL","order_id":)" << e.order_id
             << R"(,"remaining":)" << e.remaining << "}\n";
    }

    void write(const CancelAck& e) {
        out_ << R"({"event":"CANCEL_ACK","order_id":)" << e.order_id << "}\n";
    }

    void write(const Reject& e) {
        out_ << R"({"event":"REJECT","order_id":)" << e.order_id
             << R"(,"reason":")" << escape(e.reason) << "\"}\n";
    }

    void write(const STPCancel& e) {
        out_ << R"({"event":"STP_CANCEL","aggressor_id":)" << e.aggressor_id
             << R"(,"resting_id":)" << e.resting_id
             << R"(,"mode":")" << to_string(e.mode) << "\"}\n";
    }

    void write(const ModifyAck& e) {
        out_ << R"({"event":"MODIFY_ACK","order_id":)" << e.order_id
             << R"(,"new_price":)" << e.new_price
             << R"(,"new_qty":)" << e.new_qty << "}\n";
    }
};

} // namespace lob
