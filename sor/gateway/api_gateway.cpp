#include "gateway/api_gateway.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace sor::gateway
{

    // ---------------------------------------------------------------------------
    // Construction
    // ---------------------------------------------------------------------------

    ApiGateway::ApiGateway(Gateway &gateway)
        : gateway_(gateway)
    {
    }

    // ---------------------------------------------------------------------------
    // Helper: OrderState -> string for JSON output.
    // ---------------------------------------------------------------------------

    static const char *state_to_string(OrderState s) noexcept
    {
        switch (s)
        {
        case OrderState::New:
            return "new";
        case OrderState::PendingNew:
            return "pending_new";
        case OrderState::Accepted:
            return "accepted";
        case OrderState::PartiallyFilled:
            return "partially_filled";
        case OrderState::Filled:
            return "filled";
        case OrderState::PendingCancel:
            return "pending_cancel";
        case OrderState::Canceled:
            return "canceled";
        case OrderState::Rejected:
            return "rejected";
        case OrderState::Expired:
            return "expired";
        case OrderState::PendingReplace:
            return "pending_replace";
        }
        return "unknown";
    }

    static const char *side_to_string(Side s) noexcept
    {
        return s == Side::Buy ? "buy" : "sell";
    }

    // ---------------------------------------------------------------------------
    // New order
    // ---------------------------------------------------------------------------

    std::string ApiGateway::handle_new_order(const std::string &body)
    {
        json response;

        try
        {
            auto req = json::parse(body);

            Order order{};

            // Symbol (required).
            if (req.contains("symbol"))
            {
                order.symbol = Symbol(req["symbol"].get<std::string>());
            }
            else
            {
                response["status"] = "error";
                response["message"] = "missing required field: symbol";
                return response.dump();
            }

            // Side (required).
            if (req.contains("side"))
            {
                const auto s = req["side"].get<std::string>();
                order.side = (s == "sell") ? Side::Sell : Side::Buy;
            }
            else
            {
                response["status"] = "error";
                response["message"] = "missing required field: side";
                return response.dump();
            }

            // Quantity (required).
            if (req.contains("quantity"))
            {
                order.quantity = req["quantity"].get<Quantity>();
                order.remaining_quantity = order.quantity;
            }
            else
            {
                response["status"] = "error";
                response["message"] = "missing required field: quantity";
                return response.dump();
            }

            // Price (optional for market orders).
            if (req.contains("price"))
            {
                const auto p = req["price"];
                if (p.is_number_float())
                {
                    order.price = to_price(p.get<double>());
                }
                else
                {
                    order.price = p.get<Price>();
                }
            }

            // Order type.
            if (req.contains("order_type"))
            {
                const auto t = req["order_type"].get<std::string>();
                if (t == "market")
                    order.type = OrderType::Market;
                else if (t == "limit")
                    order.type = OrderType::Limit;
                else if (t == "ioc")
                    order.type = OrderType::IOC;
                else if (t == "fok")
                    order.type = OrderType::FOK;
            }

            // Time-in-force.
            if (req.contains("time_in_force"))
            {
                const auto tif = req["time_in_force"].get<std::string>();
                if (tif == "gtc")
                    order.tif = TimeInForce::GTC;
                else if (tif == "ioc")
                    order.tif = TimeInForce::IOC;
                else if (tif == "fok")
                    order.tif = TimeInForce::FOK;
                else if (tif == "day")
                    order.tif = TimeInForce::DAY;
                else if (tif == "gtd")
                    order.tif = TimeInForce::GTD;
            }

            // Routing strategy.
            if (req.contains("strategy"))
            {
                const auto strat = req["strategy"].get<std::string>();
                if (strat == "best_price")
                    order.strategy = RoutingStrategy::BestPrice;
                else if (strat == "sweep")
                    order.strategy = RoutingStrategy::LiquiditySweep;
                else if (strat == "smart_ioc")
                    order.strategy = RoutingStrategy::SmartIOC;
                else if (strat == "vwap")
                    order.strategy = RoutingStrategy::VWAP;
            }

            // Client ID.
            if (req.contains("client_id"))
            {
                order.client_id = ClientId(req["client_id"].get<std::string>());
            }

            // Client order ID.
            if (req.contains("client_order_id"))
            {
                order.client_order_id = req["client_order_id"].get<OrderId>();
            }

            // Submit.
            const OrderId assigned_id = order.id; // may be INVALID before submit
            const bool ok = gateway_.submit_order(std::move(order));

            if (ok)
            {
                response["status"] = "ok";
                response["accepted"] = true;
            }
            else
            {
                response["status"] = "error";
                response["message"] = "order submission failed (queue full or gateway not running)";
            }
        }
        catch (const json::exception &e)
        {
            response["status"] = "error";
            response["message"] = std::string("JSON parse error: ") + e.what();
        }

        return response.dump();
    }

    // ---------------------------------------------------------------------------
    // Cancel order
    // ---------------------------------------------------------------------------

    std::string ApiGateway::handle_cancel_order(const std::string &body)
    {
        json response;

        try
        {
            auto req = json::parse(body);

            CancelRequest cancel{};

            if (req.contains("order_id"))
            {
                cancel.order_id = req["order_id"].get<OrderId>();
            }
            else
            {
                response["status"] = "error";
                response["message"] = "missing required field: order_id";
                return response.dump();
            }

            if (req.contains("client_order_id"))
            {
                cancel.client_order_id = req["client_order_id"].get<OrderId>();
            }

            if (req.contains("symbol"))
            {
                cancel.symbol = Symbol(req["symbol"].get<std::string>());
            }

            if (req.contains("side"))
            {
                const auto s = req["side"].get<std::string>();
                cancel.side = (s == "sell") ? Side::Sell : Side::Buy;
            }

            const bool ok = gateway_.cancel_order(std::move(cancel));

            if (ok)
            {
                response["status"] = "ok";
                response["message"] = "cancel request submitted";
            }
            else
            {
                response["status"] = "error";
                response["message"] = "cancel request failed (queue full or gateway not running)";
            }
        }
        catch (const json::exception &e)
        {
            response["status"] = "error";
            response["message"] = std::string("JSON parse error: ") + e.what();
        }

        return response.dump();
    }

    // ---------------------------------------------------------------------------
    // Query order
    // ---------------------------------------------------------------------------

    std::string ApiGateway::handle_query_order(const std::string &body)
    {
        json response;

        try
        {
            auto req = json::parse(body);

            if (!req.contains("order_id"))
            {
                response["status"] = "error";
                response["message"] = "missing required field: order_id";
                return response.dump();
            }

            const OrderId id = req["order_id"].get<OrderId>();
            const Order *order = gateway_.get_order(id);

            if (!order)
            {
                response["status"] = "error";
                response["message"] = "order not found";
                return response.dump();
            }

            response["status"] = "ok";

            json order_json;
            order_json["order_id"] = order->id;
            order_json["client_order_id"] = order->client_order_id;
            order_json["parent_order_id"] = order->parent_order_id;
            order_json["symbol"] = order->symbol.to_string();
            order_json["side"] = side_to_string(order->side);
            order_json["state"] = state_to_string(order->state);
            order_json["price"] = to_double(order->price);
            order_json["quantity"] = order->quantity;
            order_json["filled_quantity"] = order->filled_quantity;
            order_json["remaining_quantity"] = order->remaining_quantity;
            order_json["avg_fill_price"] = to_double(order->avg_fill_price);
            order_json["venue"] = order->target_venue;
            order_json["version"] = order->version;

            // Include child order IDs if this is a parent.
            auto children = gateway_.execution_handler().get_children(id);
            if (!children.empty())
            {
                order_json["children"] = children;
            }

            response["order"] = order_json;
        }
        catch (const json::exception &e)
        {
            response["status"] = "error";
            response["message"] = std::string("JSON parse error: ") + e.what();
        }

        return response.dump();
    }

    // ---------------------------------------------------------------------------
    // Status / statistics
    // ---------------------------------------------------------------------------

    std::string ApiGateway::handle_status(const std::string & /*json*/)
    {
        json response;

        response["status"] = "ok";
        response["running"] = gateway_.is_running();

        auto gw_stats = gateway_.get_stats();
        json stats;
        stats["orders_submitted"] = gw_stats.orders_submitted;
        stats["orders_routed"] = gw_stats.orders_routed;
        stats["orders_completed"] = gw_stats.orders_completed;
        stats["orders_rejected"] = gw_stats.orders_rejected;
        response["gateway_stats"] = stats;

        auto exec_stats = gateway_.execution_handler().get_stats();
        json exec;
        exec["total_fills"] = exec_stats.total_fills;
        exec["total_partial_fills"] = exec_stats.total_partial_fills;
        exec["total_rejects"] = exec_stats.total_rejects;
        exec["total_cancels"] = exec_stats.total_cancels;
        exec["reroutes"] = exec_stats.reroutes;
        response["execution_stats"] = exec;

        response["risk_kill_switch_active"] = gateway_.risk_manager().is_kill_switch_active();

        return response.dump();
    }

} // namespace sor::gateway
