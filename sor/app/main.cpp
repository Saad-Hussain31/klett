// sor_app -- Smart Order Router main executable.
//
// Long-running, multi-threaded, event-driven trading service.
//
// Thread model (6 threads):
//   1. Main thread         — signal handling, startup, daemon wait loop, shutdown
//   2. Gateway::order      — drains MPSC order+cancel queues, routes, dispatches
//   3. Gateway::exec       — drains MPSC report queue, processes execution reports
//   4. ZmqTransport::req   — REP socket recv loop, dispatches through ApiGateway
//   5. md_feed             — continuously generates ticks from SimulatedFeedHandler
//   6. matching            — continuously runs exchange matching engines
//
// Orders arrive via ZMQ REQ/REP through ApiGateway → Gateway → RoutingEngine
// → SimulatedExchange. Execution reports flow back through lock-free queues.
// Market data and execution events are published via ZMQ PUB sockets.

#include "core/types.h"
#include "core/order.h"
#include "infra/config.h"
#include "infra/logging.h"
#include "infra/metrics.h"
#include "risk/risk_manager.h"
#include "routing/engine.h"
#include "routing/best_price.h"
#include "routing/liquidity_sweep.h"
#include "routing/smart_ioc.h"
#include "routing/vwap.h"
#include "market_data/aggregator.h"
#include "market_data/feed_handler.h"
#include "market_data/provider.h"
#include "market_data/provider_factory.h"
#include "connectors/simulated_exchange.h"
#include "gateway/gateway.h"
#include "gateway/api_gateway.h"
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
using namespace sor::infra;
using namespace sor::market_data;
using namespace sor::risk;

// ---------------------------------------------------------------------------
// Globals for signal handling
// ---------------------------------------------------------------------------
static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/)
{
    g_running.store(false, std::memory_order_release);
}

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
    auto parse_log_level = [](const std::string &s) -> LogLevel {
        if (s == "trace")    return LogLevel::Trace;
        if (s == "debug")    return LogLevel::Debug;
        if (s == "warn")     return LogLevel::Warn;
        if (s == "error")    return LogLevel::Error;
        if (s == "critical") return LogLevel::Critical;
        return LogLevel::Info;
    };

    std::string log_file = sys_config.log_file;
    if (log_file.empty())
        log_file = "logs/sor.log";

    Logger::instance().init("sor", log_file, parse_log_level(sys_config.log_level));
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

    // -- Gateway + venue connectors -------------------------------------------
    gateway::Gateway::Config gw_config;
    gw_config.config_path = config_path;
    gateway::Gateway gw(gw_config);

    // Register venues through Gateway. Keep raw pointers for matching thread.
    std::vector<SimulatedExchange *> exchange_ptrs;

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
        exchange_ptrs.push_back(exchange.get());

        gw.add_venue(std::move(exchange));
        SOR_LOG_INFO("Registered venue {} ({}) | fee={:.4f}", vc.venue_id, vc.name, vc.fee_rate);
    }

    if (exchange_ptrs.empty())
    {
        SOR_LOG_CRITICAL("No venues available, exiting");
        return 1;
    }

    // -- Risk manager ---------------------------------------------------------
    auto &risk_mgr = gw.risk_manager();
    RiskLimits global_limits;
    global_limits.max_order_quantity = sys_config.risk.max_order_quantity;
    global_limits.max_order_notional = sys_config.risk.max_order_notional;
    global_limits.max_orders_per_second = sys_config.risk.max_orders_per_second;
    global_limits.enabled = true;
    risk_mgr.set_global_limits(global_limits);

    // -- Routing engine -------------------------------------------------------
    auto &router = gw.routing_engine();
    router.register_strategy(std::make_unique<routing::BestPriceStrategy>());
    router.register_strategy(std::make_unique<routing::LiquiditySweepStrategy>());
    router.register_strategy(std::make_unique<routing::SmartIOCStrategy>());
    router.register_strategy(std::make_unique<routing::VWAPStrategy>());

    for (auto *ex : exchange_ptrs)
    {
        routing::VenueScore vs;
        vs.venue_id = ex->venue_id();
        vs.latency_us = static_cast<double>(ex->avg_latency().count());
        vs.fill_rate = 0.95;
        vs.fee_rate = 0.001;
        vs.is_available = true;
        router.update_venue_score(ex->venue_id(), vs);
    }
    SOR_LOG_INFO("Routing engine initialized with 4 strategies, {} venues", exchange_ptrs.size());

    // -- Market data feeds ----------------------------------------------------
    auto &md_aggregator = gw.market_data();
    std::vector<std::unique_ptr<SimulatedFeedHandler>> feeds;
    std::unique_ptr<MarketDataProvider> live_provider;

    const bool live_mode = (sys_config.market_data.provider != "simulated");
    const Symbol symbol("AAPL");
    const Price base_mid = to_price(150.0);

    if (live_mode)
    {
        live_provider = create_provider(sys_config.market_data);
        if (!live_provider)
        {
            SOR_LOG_CRITICAL("Failed to create market data provider '{}'",
                             sys_config.market_data.provider);
            return 1;
        }

        live_provider->set_aggregator(md_aggregator);
        constexpr VenueId alpaca_venue_id = 100;
        md_aggregator.register_venue(alpaca_venue_id);

        if (!live_provider->connect())
        {
            SOR_LOG_CRITICAL("Failed to connect to market data provider");
            return 1;
        }

        for (const auto &sym : sys_config.market_data.symbols)
            live_provider->subscribe(Symbol(sym));

        SOR_LOG_INFO("Live market data from {} — {} symbols",
                     live_provider->name(), sys_config.market_data.symbols.size());
    }
    else
    {
        for (auto *ex : exchange_ptrs)
        {
            SimulatedFeedHandler::Config fc;
            fc.symbol = symbol;
            fc.venue_id = ex->venue_id();
            fc.initial_mid_price = base_mid;
            fc.tick_size = to_price(0.01);
            fc.max_depth = 10;
            fc.base_quantity = 100;
            fc.rng_seed = 0xDEADBEEF12345678ULL;
            fc.volatility = 0.0001;

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
            for (int i = 0; i < 10; ++i)
                feed->generate_tick();

        SOR_LOG_INFO("Simulated market data initialized for {} with {} venue feeds",
                     symbol.c_str(), feeds.size());
    }

    // -- ZMQ transport + ApiGateway -------------------------------------------
    gateway::ApiGateway api_gw(gw);
    std::unique_ptr<gateway::ZmqTransport> zmq_transport;
    const bool zmq_enabled = sys_config.gateway.api.enabled;

    if (zmq_enabled)
    {
        gateway::ZmqTransport::Config zc;
        zc.order_endpoint = sys_config.gateway.api.zmq_order_endpoint;
        zc.market_data_endpoint = sys_config.gateway.api.zmq_market_data_endpoint;
        zc.execution_endpoint = sys_config.gateway.api.zmq_execution_endpoint;

        zmq_transport = std::make_unique<gateway::ZmqTransport>(zc);

        // Dispatch ZMQ requests through ApiGateway by action field
        zmq_transport->set_request_handler(
            [&api_gw](const std::string &json_body) -> std::string {
                using json = nlohmann::json;
                json response;

                try
                {
                    auto req = json::parse(json_body);
                    std::string action = req.value("action", "status");

                    if (action == "new_order")
                        return api_gw.handle_new_order(json_body);
                    else if (action == "cancel_order")
                        return api_gw.handle_cancel_order(json_body);
                    else if (action == "query_order")
                        return api_gw.handle_query_order(json_body);
                    else if (action == "status")
                        return api_gw.handle_status(json_body);
                    else
                    {
                        response["status"] = "error";
                        response["message"] = "unknown action: " + action;
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
            SOR_LOG_INFO("ZMQ transport started: orders={} md={} exec={}",
                         zc.order_endpoint, zc.market_data_endpoint, zc.execution_endpoint);
        else
            SOR_LOG_WARN("ZMQ transport failed to start");
    }
    else
    {
        SOR_LOG_INFO("ZMQ transport disabled (set gateway.api.enabled=true in config)");
    }

    // -- Wire NBBO publishing via ZMQ PUB ------------------------------------
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

    // -- Wire fill/completion observers for ZMQ PUB --------------------------
    gw.set_fill_observer(
        [&zmq_transport, zmq_enabled](const Order &order, const ExecutionReport &report) {
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

    gw.set_completion_observer(
        [&zmq_transport, zmq_enabled](const Order &order) {
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

    // -- Initialize and start Gateway -----------------------------------------
    if (!gw.initialize())
    {
        SOR_LOG_CRITICAL("Gateway initialization failed");
        return 1;
    }
    gw.start();
    SOR_LOG_INFO("Gateway started (order_thread + exec_thread running)");

    // -- Market data feed thread (NEW) ----------------------------------------
    std::thread md_feed_thread([&feeds, &symbol, &md_aggregator]() {
        SOR_LOG_INFO("Market data feed thread started");

        while (g_running.load(std::memory_order_relaxed))
        {
            for (auto &feed : feeds)
                feed->generate_tick();

            // TODO (Saad): REMOVE - Market data debug
            auto nbbo = md_aggregator.get_nbbo(symbol);
            if (nbbo.valid())
            {
                SOR_LOG_DEBUG("TODO (Saad): REMOVE - Market data debug | "
                              "NBBO bid={:.2f} x {} ask={:.2f} x {} spread={:.4f}",
                              to_double(nbbo.best_bid), nbbo.best_bid_qty,
                              to_double(nbbo.best_ask), nbbo.best_ask_qty,
                              to_double(nbbo.spread()));
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        SOR_LOG_INFO("Market data feed thread stopped");
    });

    // -- Matching engine thread (NEW) -----------------------------------------
    std::thread matching_thread([&exchange_ptrs, &symbol, &md_aggregator]() {
        SOR_LOG_INFO("Matching engine thread started");

        while (g_running.load(std::memory_order_relaxed))
        {
            // Update exchange market prices from current NBBO
            auto nbbo = md_aggregator.get_nbbo(symbol);
            if (nbbo.valid())
            {
                for (auto *ex : exchange_ptrs)
                    ex->set_market_price(nbbo.best_bid, nbbo.best_ask);
            }

            // Run matching on all exchanges
            for (auto *ex : exchange_ptrs)
                ex->process_matching();

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        SOR_LOG_INFO("Matching engine thread stopped");
    });

    // -- Daemon wait loop -----------------------------------------------------
    SOR_LOG_INFO("=== SOR Service Running ===");
    SOR_LOG_INFO("Threads: main, gateway-order, gateway-exec, zmq-request, md-feed, matching");
    SOR_LOG_INFO("Listening for orders on {} (ZMQ REQ/REP)",
                 sys_config.gateway.api.zmq_order_endpoint);
    SOR_LOG_INFO("Publishing market data on {} (ZMQ PUB)",
                 sys_config.gateway.api.zmq_market_data_endpoint);
    SOR_LOG_INFO("Publishing execution events on {} (ZMQ PUB)",
                 sys_config.gateway.api.zmq_execution_endpoint);
    SOR_LOG_INFO("Waiting for SIGINT/SIGTERM to shut down...");

    while (g_running.load(std::memory_order_relaxed))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // -- Shutdown -------------------------------------------------------------
    SOR_LOG_INFO("Shutdown signal received, stopping services...");

    // Stop new threads first (they check g_running)
    if (md_feed_thread.joinable())
        md_feed_thread.join();
    SOR_LOG_INFO("Market data feed thread joined");

    if (matching_thread.joinable())
        matching_thread.join();
    SOR_LOG_INFO("Matching engine thread joined");

    // Stop ZMQ transport (joins request_thread_)
    if (zmq_transport)
        zmq_transport->stop();
    SOR_LOG_INFO("ZMQ transport stopped");

    // Stop live provider if active
    if (live_provider)
        live_provider->disconnect();

    // Stop feeds
    for (auto &feed : feeds)
        feed->stop();

    // Stop Gateway (joins order_thread_ + exec_thread_, disconnects venues)
    gw.stop();
    SOR_LOG_INFO("Gateway stopped");

    // Metrics cleanup
    if (sys_config.metrics.enabled || sys_config.enable_metrics)
        MetricsManager::instance().shutdown();

    SOR_LOG_INFO("Smart Order Router stopped cleanly");
    return 0;
}
