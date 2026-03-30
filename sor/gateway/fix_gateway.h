#pragma once

// Simplified FIX gateway interface.
//
// Translates inbound FIX-style messages into Order objects submitted to
// the core Gateway, and converts outbound ExecutionReports back into
// FIX message strings for transmission.  This is a mock/simplified
// implementation -- a production FIX engine would use a full FIX parser
// and session management layer.

#include "gateway/gateway.h"
#include <functional>
#include <string>

namespace sor::gateway
{

    class FixGateway
    {
    public:
        explicit FixGateway(Gateway &gateway);

        // Accept a raw FIX message, parse it, and submit to the Gateway.
        // Supported message types:
        //   - D (NewOrderSingle)
        //   - F (OrderCancelRequest)
        void on_fix_message(const std::string &raw_fix);

        // Set callback for outgoing FIX messages (execution reports, acks).
        void set_send_callback(std::function<void(const std::string &)> cb);

    private:
        // Parse a FIX tag=value pair from the message.
        static std::string get_tag(const std::string &msg, const std::string &tag);

        // Build an outbound FIX execution report string.
        std::string build_execution_report(const Order &order, const ExecutionReport &report);

        Gateway &gateway_;
        std::function<void(const std::string &)> send_callback_;
    };

} // namespace sor::gateway
