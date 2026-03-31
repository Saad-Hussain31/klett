// sor_app -- Smart Order Router main executable.
//
// Wires together the full end-to-end pipeline:
//   config -> logging -> metrics -> market data -> risk -> routing
//   -> venue connectors -> execution handler -> fill reports
//
// In simulation mode (default), the app:
//   1. Loads config from YAML (or uses built-in defaults)
//   2. Creates simulated exchanges with configurable fill behavior
//   3. Generates synthetic market data
//   4. Injects a batch of test orders
//   5. Routes, executes, and reports results
//   6. Prints statistics and exits cleanly

#include "core/types.h"
#include "core/order.h"
#include "infra/config.h"
#include "infra/logging.h"
#include "infra/metrics.h"
#include "risk/risk_manager.h"
#include "risk/rate_limiter.h"
#include "routing/engine.h"
#include "routing/best_price.h"
#include "routing/liquidity_sweep.h"
#include "routing/smart_ioc.h"
#include "routing/vwap.h"
#include "market_data/aggregator.h"
#include "market_data/feed_handler.h"
#include "market_data/book.h"
#include "connectors/simulated_exchange.h"
#include "execution/execution_handler.h"
#include "state/order_state_machine.h"
#include "gateway/zmq_transport.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace sor;
using namespace sor::connectors;
using namespace sor::execution;
using namespace sor::infra;
using namespace sor::market_data;
using namespace sor::risk;
// Note: NOT using namespace sor::routing to avoid RoutingStrategy
// name clash between the enum (sor::RoutingStrategy) and the class
// (sor::routing::RoutingStrategy).

// ---------------------------------------------------------------------------
// Globals for signal handling
// ---------------------------------------------------------------------------
static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/)
{
    g_running.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// Per-venue rate limiter wrapper
// ---------------------------------------------------------------------------
struct VenueConnection
{
    std::unique_ptr<SimulatedExchange> exchange;
    RateLimiter rate_limiter;
    VenueId id;

    VenueConnection(std::unique_ptr<SimulatedExchange> ex, int32_t max_rate, VenueId vid)
        : exchange(std::move(ex)), rate_limiter(max_rate), id(vid) {}

    VenueConnection(VenueConnection &&o) noexcept
        : exchange(std::move(o.exchange)),
          rate_limiter(o.rate_limiter.max_rate()),
          id(o.id) {}

    VenueConnection &operator=(VenueConnection &&) = delete;
    VenueConnection(const VenueConnection &) = delete;
    VenueConnection &operator=(const VenueConnection &) = delete;
};

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    // -- Signal handling ------------------------------------------------------
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // -- Config ---------------------------------------------------------------
    std::string config_path;
    if (argc > 1)
        config_path = argv[1];

    auto &config_mgr = ConfigManager::instance();
    SystemConfig sys_config;

    if (!config_path.empty() && config_mgr.load(config_path))
    {
        sys_config = config_mgr.get_config();
        std::cout << "[init] Loaded config from " << config_path << "\n";

        // Validate: fail fast on bad config
        auto err = validate_config(sys_config);
        if (!err.empty())
        {
            std::cerr << "[FATAL] Config validation failed: " << err << "\n";
            return 1;
        }
        std::cout << "[init] Config validated: " << sys_config.venues.size() << " venues, "
                  << "gateway.api=" << (sys_config.gateway.api.enabled ? "on" : "off")
                  << " metrics=" << (sys_config.metrics.enabled ? "on" : "off") << "\n";
    }
    else
    {
        // Built-in defaults for simulation mode
        sys_config.log_level = "info";
        sys_config.enable_metrics = false;

        VenueConfig v1;
        v1.venue_id = 1;
        v1.name = "NYSE";
        v1.type = "simulated";
        v1.fee_rate = 0.001;
        v1.enabled = true;
        v1.max_orders_per_second = 500;

        VenueConfig v2;
        v2.venue_id = 2;
        v2.name = "NASDAQ";
        v2.type = "simulated";
        v2.fee_rate = 0.0008;
        v2.enabled = true;
        v2.max_orders_per_second = 800;

        VenueConfig v3;
        v3.venue_id = 3;
        v3.name = "BATS";
        v3.type = "simulated";
        v3.fee_rate = 0.0012;
        v3.enabled = true;
        v3.max_orders_per_second = 600;

        sys_config.venues = {v1, v2, v3};
        sys_config.risk.max_order_quantity = 100000;
        sys_config.risk.max_orders_per_second = 1000;

        std::cout << "[init] Using built-in simulation defaults (pass config YAML as arg)\n";
    }

    // -- Logging --------------------------------------------------------------
    Logger::instance().init("sor", sys_config.log_file,
                            sys_config.log_level == "debug"    ? LogLevel::Debug
                            : sys_config.log_level == "trace"  ? LogLevel::Trace
                            : sys_config.log_level == "warn"   ? LogLevel::Warn
                            : sys_config.log_level == "error"  ? LogLevel::Error
                                                               : LogLevel::Info);
    SOR_LOG_INFO("Smart Order Router starting");

    // -- Metrics (optional) ---------------------------------------------------
    if (sys_config.metrics.enabled || sys_config.enable_metrics)
    {
        int metrics_port = sys_config.metrics.port;
        if (MetricsManager::instance().init(metrics_port))
            SOR_LOG_INFO("Prometheus metrics on {}:{}{}", sys_config.metrics.bind_address,
                         metrics_port, sys_config.metrics.path);
        else
            SOR_LOG_WARN("Failed to start metrics endpoint on port {}", metrics_port);
    }

    // -- Venue connectors with per-venue rate limiters ------------------------
    std::vector<VenueConnection> venues;
    for (const auto &vc : sys_config.venues)
    {
        if (!vc.enabled)
        {
            SOR_LOG_INFO("Venue {} ({}) disabled, skipping", vc.venue_id, vc.name);
            continue;
        }

        SimulatedExchange::Config exc;
        exc.venue_id = vc.venue_id;
        exc.name = vc.name;
        exc.fill_probability = 0.95;
        exc.partial_fill_probability = 0.3;
        exc.reject_probability = 0.01;
        exc.fee_rate = vc.fee_rate;

        auto exchange = std::make_unique<SimulatedExchange>(exc);
        if (!exchange->connect())
        {
            SOR_LOG_ERROR("Failed to connect venue {} ({})", vc.venue_id, vc.name);
            continue;
        }

        SOR_LOG_INFO("Connected venue {} ({}) | rate_limit={}/s fee={:.4f}",
                     vc.venue_id, vc.name, vc.max_orders_per_second, vc.fee_rate);
        venues.emplace_back(std::move(exchange), vc.max_orders_per_second, vc.venue_id);
    }

    if (venues.empty())
    {
        SOR_LOG_CRITICAL("No venues available, exiting");
        return 1;
    }

    // -- Market data ----------------------------------------------------------
    MarketDataAggregator md_aggregator;
    std::vector<std::unique_ptr<SimulatedFeedHandler>> feeds;

    const Symbol symbol("AAPL");
    const Price base_mid = to_price(150.0);

    for (auto &vc : venues)
    {
        md_aggregator.register_venue(vc.id);

        SimulatedFeedHandler::Config fc;
        fc.symbol = symbol;
        fc.venue_id = vc.id;
        fc.initial_mid_price = base_mid;
        fc.tick_size = to_price(0.01);
        fc.max_depth = 10;
        fc.base_quantity = 100;
        // Use closely-related seeds that produce correlated but slightly
        // different books per venue (like real market microstructure).
        fc.rng_seed = 0xDEADBEEF12345678ULL;
        fc.volatility = 0.0001; // low volatility so venues stay in sync

        auto feed = std::make_unique<SimulatedFeedHandler>(fc);
        feed->set_book_callback(
            [&md_aggregator](VenueId vid, const Symbol &sym, const OrderBook &book) {
                md_aggregator.on_book_update(vid, sym, book);
            });
        feed->start();
        feeds.push_back(std::move(feed));
    }

    // Generate initial ticks to populate order books
    for (auto &feed : feeds)
    {
        for (int i = 0; i < 10; ++i)
            feed->generate_tick();
    }

    SOR_LOG_INFO("Market data initialized for {} with {} venue feeds",
                 symbol.c_str(), feeds.size());

    // -- Risk manager ---------------------------------------------------------
    RiskManager risk_mgr;
    RiskLimits global_limits;
    global_limits.max_order_quantity = sys_config.risk.max_order_quantity;
    global_limits.max_order_notional = sys_config.risk.max_order_notional;
    global_limits.max_orders_per_second = sys_config.risk.max_orders_per_second;
    global_limits.enabled = true;
    risk_mgr.set_global_limits(global_limits);

    // -- Routing engine -------------------------------------------------------
    routing::RoutingEngine router(md_aggregator, risk_mgr);
    router.register_strategy(std::make_unique<routing::BestPriceStrategy>());
    router.register_strategy(std::make_unique<routing::LiquiditySweepStrategy>());
    router.register_strategy(std::make_unique<routing::SmartIOCStrategy>());
    router.register_strategy(std::make_unique<routing::VWAPStrategy>());

    // Register venue scores
    for (auto &vc : venues)
    {
        routing::VenueScore vs;
        vs.venue_id = vc.id;
        vs.latency_us = static_cast<double>(vc.exchange->avg_latency().count());
        vs.fill_rate = 0.95;
        vs.fee_rate = 0.001;
        vs.is_available = true;
        router.update_venue_score(vc.id, vs);
    }

    SOR_LOG_INFO("Routing engine initialized with 4 strategies");

    // -- Execution handler ----------------------------------------------------
    ExecutionHandler exec_handler;

    // Wire venue execution callbacks -> execution handler
    for (auto &vc : venues)
    {
        vc.exchange->set_execution_callback(
            [&exec_handler](const ExecutionReport &report) {
                exec_handler.on_execution_report(report);
            });
    }

    // Execution handler callbacks (set after ZMQ transport is configured below)
    std::atomic<uint64_t> total_fills{0};
    std::atomic<uint64_t> total_completions{0};

    // Reroute tracking: collect reroute requests, process them in the main loop
    // to avoid re-entrant send_order -> exec_callback -> reroute chains.
    std::vector<OrderId> pending_reroutes;
    std::mutex reroute_mutex;

    exec_handler.set_reroute_callback(
        [&pending_reroutes, &reroute_mutex](Order &parent) {
            std::lock_guard lk(reroute_mutex);
            pending_reroutes.push_back(parent.id);
        });

    // -- ZMQ transport (real IPC) ---------------------------------------------
    std::unique_ptr<gateway::ZmqTransport> zmq_transport;
    bool zmq_enabled = sys_config.gateway.api.enabled;

    if (zmq_enabled)
    {
        gateway::ZmqTransport::Config zc;
        zc.order_endpoint = sys_config.gateway.api.zmq_order_endpoint;
        zc.market_data_endpoint = sys_config.gateway.api.zmq_market_data_endpoint;
        zc.execution_endpoint = sys_config.gateway.api.zmq_execution_endpoint;

        zmq_transport = std::make_unique<gateway::ZmqTransport>(zc);

        // Wire the request handler to process JSON order submissions
        zmq_transport->set_request_handler(
            [&router, &exec_handler](const std::string &json_body)
                -> std::string {
                using json = nlohmann::json;
                json response;

                try
                {
                    auto req = json::parse(json_body);
                    std::string action = req.value("action", "status");

                    if (action == "status")
                    {
                        auto rs = router.get_stats();
                        auto es = exec_handler.get_stats();
                        response["status"] = "ok";
                        response["orders_routed"] = rs.orders_routed;
                        response["orders_rejected"] = rs.orders_rejected;
                        response["total_fills"] = es.total_fills;
                        response["total_partial_fills"] = es.total_partial_fills;
                    }
                    else
                    {
                        response["status"] = "ok";
                        response["message"] = "action not implemented in simulation mode";
                    }
                }
                catch (const std::exception &e)
                {
                    response["status"] = "error";
                    response["message"] = e.what();
                }

                return response.dump();
            });

        if (zmq_transport->start())
            SOR_LOG_INFO("ZMQ transport enabled: orders={} md={} exec={}",
                         sys_config.gateway.api.zmq_order_endpoint,
                         sys_config.gateway.api.zmq_market_data_endpoint,
                         sys_config.gateway.api.zmq_execution_endpoint);
        else
            SOR_LOG_WARN("ZMQ transport failed to start");
    }
    else
    {
        SOR_LOG_INFO("ZMQ transport disabled (set gateway.api.enabled=true in config)");
    }

    // Wire market data publisher: publish NBBO updates via ZMQ PUB
    md_aggregator.set_nbbo_callback(
        [&zmq_transport, zmq_enabled](const Symbol &sym, const market_data::NBBO &nbbo) {
            if (!zmq_enabled)
                return;

            using json = nlohmann::json;
            json payload;
            payload["symbol"] = sym.to_string();
            payload["best_bid"] = to_double(nbbo.best_bid);
            payload["best_ask"] = to_double(nbbo.best_ask);
            payload["best_bid_qty"] = nbbo.best_bid_qty;
            payload["best_ask_qty"] = nbbo.best_ask_qty;
            payload["bid_venue"] = nbbo.best_bid_venue;
            payload["ask_venue"] = nbbo.best_ask_venue;
            payload["spread"] = to_double(nbbo.spread());

            zmq_transport->publish_market_data(sym.to_string(), payload.dump());
        });

    // Wire execution event publisher: publish fills/completions via ZMQ PUB
    exec_handler.set_fill_callback(
        [&total_fills, &zmq_transport, zmq_enabled](const Order &order, const ExecutionReport &report) {
            total_fills.fetch_add(1, std::memory_order_relaxed);
            SOR_LOG_DEBUG("FILL order={} qty={} price={:.2f} cum={}/{}",
                          order.id, report.last_quantity,
                          to_double(report.last_price),
                          report.cum_quantity, order.quantity);

            if (!zmq_enabled)
                return;

            using json = nlohmann::json;
            json payload;
            payload["event"] = "fill";
            payload["order_id"] = order.id;
            payload["symbol"] = order.symbol.to_string();
            payload["last_price"] = to_double(report.last_price);
            payload["last_quantity"] = report.last_quantity;
            payload["cum_quantity"] = report.cum_quantity;
            payload["leaves_quantity"] = report.leaves_quantity;
            payload["venue_id"] = report.venue_id;

            zmq_transport->publish_execution_event("FILL", payload.dump());
        });

    exec_handler.set_completion_callback(
        [&total_completions, &zmq_transport, zmq_enabled](const Order &order) {
            total_completions.fetch_add(1, std::memory_order_relaxed);
            SOR_LOG_INFO("COMPLETE order={} filled={} avg_price={:.2f}",
                         order.id, order.filled_quantity,
                         to_double(order.avg_fill_price));

            if (!zmq_enabled)
                return;

            using json = nlohmann::json;
            json payload;
            payload["event"] = "complete";
            payload["order_id"] = order.id;
            payload["symbol"] = order.symbol.to_string();
            payload["filled_quantity"] = order.filled_quantity;
            payload["avg_fill_price"] = to_double(order.avg_fill_price);

            zmq_transport->publish_execution_event("COMPLETE", payload.dump());
        });

    // -- Generate and route orders --------------------------------------------
    // Set initial exchange market prices from NBBO
    {
        auto nbbo = md_aggregator.get_nbbo(symbol);
        if (nbbo.valid())
        {
            for (auto &vc : venues)
                vc.exchange->set_market_price(nbbo.best_bid, nbbo.best_ask);
        }
    }
    SOR_LOG_INFO("=== Starting simulation ===");

    constexpr int NUM_ORDERS = 20;
    std::atomic<OrderId> order_id_gen{1};

    // -- Helper: create child order from a routing slice and send to venue ----
    auto send_child = [&](OrderId parent_id, const Order &parent,
                          const auto &slice) -> bool {
        Order child{};
        child.id = order_id_gen.fetch_add(1, std::memory_order_relaxed);
        child.parent_order_id = parent_id;
        child.symbol = parent.symbol;
        child.side = parent.side;
        child.type = slice.type;
        child.tif = slice.tif;
        child.price = slice.price;
        child.quantity = slice.quantity;
        child.remaining_quantity = slice.quantity;
        child.target_venue = slice.venue_id;
        child.state = OrderState::New;
        child.create_time = std::chrono::steady_clock::now();

        state::OrderStateMachine::apply(child, state::OrderEvent::Submit);
        exec_handler.track_child_order(parent_id, child);

        for (auto &vc : venues)
        {
            if (vc.id == slice.venue_id)
            {
                if (vc.rate_limiter.try_acquire())
                {
                    vc.exchange->send_order(child);
                    return true;
                }
                SOR_LOG_WARN("Rate limit hit venue={} order={}", vc.id, child.id);
                return false;
            }
        }
        return false;
    };

    struct OrderResult
    {
        OrderId id;
        sor::RoutingStrategy strategy;
        int child_count;
        bool routed;
    };
    std::vector<OrderResult> results;

    for (int i = 0; i < NUM_ORDERS && g_running.load(std::memory_order_relaxed); ++i)
    {
        // Refresh market data
        for (auto &feed : feeds)
            feed->generate_tick();

        // Create parent order
        Order order{};
        order.id = order_id_gen.fetch_add(1, std::memory_order_relaxed);
        order.symbol = symbol;
        order.side = (i % 2 == 0) ? Side::Buy : Side::Sell;
        order.type = OrderType::Limit;
        order.tif = TimeInForce::GTC;
        order.quantity = 50 + (i * 10);
        order.remaining_quantity = order.quantity;
        order.state = OrderState::New;
        order.create_time = std::chrono::steady_clock::now();

        // Assign strategy round-robin
        switch (i % 4)
        {
        case 0: order.strategy = sor::RoutingStrategy::BestPrice; break;
        case 1: order.strategy = sor::RoutingStrategy::LiquiditySweep; break;
        case 2: order.strategy = sor::RoutingStrategy::SmartIOC; break;
        case 3: order.strategy = sor::RoutingStrategy::VWAP; break;
        }

        // Set price based on NBBO
        auto nbbo = md_aggregator.get_nbbo(symbol);
        if (nbbo.valid())
        {
            order.price = (order.side == Side::Buy) ? nbbo.best_ask : nbbo.best_bid;
        }
        else
        {
            order.price = base_mid;
        }

        // Submit through state machine
        state::OrderStateMachine::apply(order, state::OrderEvent::Submit);

        // Route
        auto decision = router.route_order(order);

        OrderResult result;
        result.id = order.id;
        result.strategy = order.strategy;
        result.child_count = 0;
        result.routed = decision.valid();

        if (decision.valid())
        {
            exec_handler.track_order(order);

            for (const auto &slice : decision.slices)
            {
                if (send_child(order.id, order, slice))
                    ++result.child_count;
            }
        }
        else
        {
            SOR_LOG_WARN("Order {} rejected by router", order.id);
        }

        results.push_back(result);
    }

    // -- Helper: process pending reroutes ------------------------------------
    auto process_reroutes = [&]() {
        std::vector<OrderId> to_reroute;
        {
            std::lock_guard lk(reroute_mutex);
            to_reroute.swap(pending_reroutes);
        }

        for (OrderId pid : to_reroute)
        {
            Order *parent = exec_handler.get_mutable_order(pid);
            if (!parent || parent->is_terminal())
                continue;

            SOR_LOG_INFO("REROUTE order={} remaining={}", parent->id, parent->remaining_quantity);
            auto decision = router.route_order(*parent);
            if (!decision.valid())
            {
                SOR_LOG_WARN("Reroute failed for order {}", parent->id);
                continue;
            }

            for (const auto &slice : decision.slices)
                send_child(parent->id, *parent, slice);
        }
    };

    // -- Run matching engines -------------------------------------------------
    SOR_LOG_INFO("Processing matching engines...");
    for (int cycle = 0; cycle < 5; ++cycle)
    {
        for (auto &feed : feeds)
            feed->generate_tick();

        // Update exchange market prices from NBBO so matching engine fills
        auto nbbo = md_aggregator.get_nbbo(symbol);
        if (nbbo.valid())
        {
            for (auto &vc : venues)
                vc.exchange->set_market_price(nbbo.best_bid, nbbo.best_ask);
        }

        for (auto &vc : venues)
            vc.exchange->process_matching();
        process_reroutes();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // -- Print results --------------------------------------------------------
    std::cout << "\n";
    std::cout << "============================================================\n";
    std::cout << "                    SIMULATION RESULTS\n";
    std::cout << "============================================================\n";

    auto strategy_name = [](sor::RoutingStrategy s) -> const char * {
        switch (s)
        {
        case sor::RoutingStrategy::BestPrice: return "BestPrice";
        case sor::RoutingStrategy::LiquiditySweep: return "LiqSweep";
        case sor::RoutingStrategy::SmartIOC: return "SmartIOC";
        case sor::RoutingStrategy::VWAP: return "VWAP";
        default: return "Unknown";
        }
    };

    std::cout << "\n  Order Results:\n";
    std::cout << "  -------------------------------------------------------\n";
    for (const auto &r : results)
    {
        auto tracked = exec_handler.get_order(r.id);
        const char *state_str = "N/A";
        Quantity filled = 0;
        Quantity total = 0;
        if (tracked)
        {
            state_str = state::OrderStateMachine::to_string(tracked->state);
            filled = tracked->filled_quantity;
            total = tracked->quantity;
        }

        std::cout << "  Order " << r.id
                  << " | " << strategy_name(r.strategy)
                  << " | children=" << r.child_count
                  << " | " << (r.routed ? "routed" : "REJECTED")
                  << " | state=" << state_str
                  << " | filled=" << filled << "/" << total
                  << "\n";
    }

    // Venue stats
    std::cout << "\n  Venue Statistics:\n";
    std::cout << "  -------------------------------------------------------\n";
    for (auto &vc : venues)
    {
        auto stats = vc.exchange->get_stats();
        std::cout << "  Venue " << vc.id
                  << " | recv=" << stats.orders_received
                  << " filled=" << stats.orders_filled
                  << " partial=" << stats.orders_partially_filled
                  << " reject=" << stats.orders_rejected
                  << " cancel=" << stats.orders_canceled
                  << " | latency=" << vc.exchange->avg_latency().count() << "us"
                  << "\n";
    }

    // Execution handler stats
    auto exec_stats = exec_handler.get_stats();
    std::cout << "\n  Execution Summary:\n";
    std::cout << "  -------------------------------------------------------\n";
    std::cout << "  Total fills:         " << exec_stats.total_fills << "\n";
    std::cout << "  Partial fills:       " << exec_stats.total_partial_fills << "\n";
    std::cout << "  Rejects:             " << exec_stats.total_rejects << "\n";
    std::cout << "  Cancels:             " << exec_stats.total_cancels << "\n";
    std::cout << "  Reroutes:            " << exec_stats.reroutes << "\n";
    std::cout << "  Completions:         " << total_completions.load() << "\n";

    // Routing engine stats
    auto route_stats = router.get_stats();
    std::cout << "\n  Routing Summary:\n";
    std::cout << "  -------------------------------------------------------\n";
    std::cout << "  Orders routed:       " << route_stats.orders_routed << "\n";
    std::cout << "  Orders rejected:     " << route_stats.orders_rejected << "\n";
    std::cout << "  Total slices:        " << route_stats.total_slices << "\n";

    std::cout << "\n============================================================\n";

    // ZMQ transport stats
    if (zmq_enabled)
    {
        auto zs = zmq_transport->get_stats();
        std::cout << "\n  ZMQ Transport:\n";
        std::cout << "  -------------------------------------------------------\n";
        std::cout << "  Requests received:   " << zs.requests_received << "\n";
        std::cout << "  Requests handled:    " << zs.requests_handled << "\n";
        std::cout << "  MD published:        " << zs.md_published << "\n";
        std::cout << "  Exec published:      " << zs.exec_published << "\n";
        std::cout << "  Errors:              " << zs.errors << "\n";
        std::cout << "============================================================\n";
    }

    // -- Cleanup --------------------------------------------------------------
    SOR_LOG_INFO("Shutting down...");
    if (zmq_transport)
        zmq_transport->stop();
    for (auto &feed : feeds)
        feed->stop();
    for (auto &vc : venues)
        vc.exchange->disconnect();
    if (sys_config.metrics.enabled || sys_config.enable_metrics)
        MetricsManager::instance().shutdown();

    SOR_LOG_INFO("Smart Order Router stopped");
    return 0;
}
