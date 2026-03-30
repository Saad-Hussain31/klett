#pragma once

// Simplified JSON API gateway.
//
// Accepts JSON-encoded order requests, translates them into the
// internal Order type, and dispatches them through the core Gateway.
// Query endpoints return JSON-encoded responses.

#include "gateway/gateway.h"
#include <string>

namespace sor::gateway
{

    class ApiGateway
    {
    public:
        explicit ApiGateway(Gateway &gateway);

        // Accept a JSON new-order request, submit to Gateway.
        // Returns a JSON response with order_id or error.
        std::string handle_new_order(const std::string &json);

        // Accept a JSON cancel request.
        // Returns a JSON response with status.
        std::string handle_cancel_order(const std::string &json);

        // Query an order by ID.
        // Returns a JSON response with the order state.
        std::string handle_query_order(const std::string &json);

        // Query gateway status / statistics.
        // Returns a JSON response with system stats.
        std::string handle_status(const std::string &json);

    private:
        Gateway &gateway_;
    };

} // namespace sor::gateway
