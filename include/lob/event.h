#pragma once

#include "types.h"
#include <variant>
#include <vector>
#include <string>

namespace lob {

struct Ack       { OrderId order_id; };
struct Trade     { OrderId aggressor_id; OrderId resting_id; Price price; Qty qty; Timestamp ts; };
struct Filled    { OrderId order_id; };
struct Partial   { OrderId order_id; Qty remaining; };
struct CancelAck { OrderId order_id; };
struct Reject    { OrderId order_id; std::string reason; };
struct STPCancel { OrderId aggressor_id; OrderId resting_id; STPMode mode; };
struct ModifyAck { OrderId order_id; Price new_price; Qty new_qty; };

using Event = std::variant<Ack, Trade, Filled, Partial, CancelAck, Reject, STPCancel, ModifyAck>;

// sink interface — test code collects into a vector, prod code logs to file
class EventSink {
public:
    virtual ~EventSink() = default;
    virtual void on_event(const Event& event) = 0;
};

class VectorSink : public EventSink {
public:
    std::vector<Event> events;
    void on_event(const Event& event) override { events.push_back(event); }
    void clear() { events.clear(); }
    size_t size() const { return events.size(); }
};

} // namespace lob
