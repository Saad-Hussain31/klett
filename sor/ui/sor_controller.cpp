#include "ui/sor_controller.h"
#include "infra/config.h"
#include "infra/logging.h"
#include "infra/metrics.h"
#include "routing/engine.h"
#include "routing/best_price.h"
#include "routing/liquidity_sweep.h"
#include "routing/smart_ioc.h"
#include "routing/vwap.h"
#include "market_data/feed_handler.h"
#include "market_data/provider.h"
#include "market_data/provider_factory.h"
#include "connectors/simulated_exchange.h"

#include <spdlog/sinks/base_sink.h>
#include <spdlog/details/null_mutex.h>

#include <chrono>
#include <iostream>

using namespace sor::infra;
using namespace sor::connectors;
using namespace sor::market_data;
using namespace sor::risk;

namespace sor::ui
{

    // ── Custom spdlog sink that captures log messages for the UI ──────────────

    class UiLogSink : public spdlog::sinks::base_sink<std::mutex>
    {
    public:
        explicit UiLogSink(std::deque<LogMessage> &buffer, std::mutex &mtx)
            : buffer_(buffer), buffer_mutex_(mtx) {}

    protected:
        void sink_it_(const spdlog::details::log_msg &msg) override
        {
            spdlog::memory_buf_t formatted;
            formatter_->format(msg, formatted);

            LogMessage lm;
            lm.text = std::string(formatted.data(), formatted.size());
            lm.level = static_cast<int>(msg.level);

            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffer_.push_back(std::move(lm));
            if (buffer_.size() > 2000)
                buffer_.pop_front();
        }

        void flush_() override {}

    private:
        std::deque<LogMessage> &buffer_;
        std::mutex &buffer_mutex_;
    };

    // ── SorController implementation ──────────────────────────────────────────

    SorController::SorController() = default;

    SorController::~SorController()
    {
        stop();
    }

    bool SorController::initialize(const std::string &config_path)
    {
        setup_backend(config_path);
        if (!gateway_)
            return false;

        wire_observers();

        if (!gateway_->initialize())
            return false;

        return true;
    }

    void SorController::setup_backend(const std::string &config_path)
    {
        auto &config_mgr = ConfigManager::instance();
        SystemConfig sys_config;

        if (!config_path.empty() && config_mgr.load(config_path))
        {
            sys_config = config_mgr.get_config();
            auto err = validate_config(sys_config);
            if (!err.empty())
            {
                std::cerr << "[FATAL] Config validation failed: " << err << "\n";
                return;
            }
        }
        else
        {
            sys_config.log_level = "info";
            sys_config.enable_metrics = false;

            VenueConfig v1;
            v1.venue_id = 1; v1.name = "NYSE"; v1.type = "simulated";
            v1.fee_rate = 0.001; v1.enabled = true; v1.max_orders_per_second = 500;

            VenueConfig v2;
            v2.venue_id = 2; v2.name = "NASDAQ"; v2.type = "simulated";
            v2.fee_rate = 0.0008; v2.enabled = true; v2.max_orders_per_second = 800;

            VenueConfig v3;
            v3.venue_id = 3; v3.name = "BATS"; v3.type = "simulated";
            v3.fee_rate = 0.0012; v3.enabled = true; v3.max_orders_per_second = 600;

            sys_config.venues = {v1, v2, v3};
            sys_config.risk.max_order_quantity = 100000;
            sys_config.risk.max_orders_per_second = 1000;
        }

        // Logging
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

        // Attach UI log sink
        log_sink_ = std::make_shared<UiLogSink>(log_messages_, log_mutex_);
        auto logger = Logger::instance().get();
        if (logger)
            logger->sinks().push_back(log_sink_);

        SOR_LOG_INFO("SorController initializing");

        // Gateway + venue connectors
        gateway::Gateway::Config gw_config;
        gw_config.config_path = config_path;
        gateway_ = std::make_unique<gateway::Gateway>(gw_config);

        for (const auto &vc : sys_config.venues)
        {
            if (!vc.enabled) continue;

            SimulatedExchange::Config exc;
            exc.venue_id = vc.venue_id;
            exc.name = vc.name;
            exc.fill_probability = 0.95;
            exc.partial_fill_probability = 0.3;
            exc.reject_probability = 0.01;
            exc.fee_rate = vc.fee_rate;

            auto exchange = std::make_unique<SimulatedExchange>(exc);
            exchange_ptrs_.push_back(exchange.get());
            gateway_->add_venue(std::move(exchange));
        }

        if (exchange_ptrs_.empty())
        {
            SOR_LOG_CRITICAL("No venues available");
            gateway_.reset();
            return;
        }

        // Risk manager
        auto &risk_mgr = gateway_->risk_manager();
        RiskLimits global_limits;
        global_limits.max_order_quantity = sys_config.risk.max_order_quantity;
        global_limits.max_order_notional = sys_config.risk.max_order_notional;
        global_limits.max_orders_per_second = sys_config.risk.max_orders_per_second;
        global_limits.enabled = true;
        risk_mgr.set_global_limits(global_limits);

        // Routing engine
        auto &router = gateway_->routing_engine();
        router.register_strategy(std::make_unique<routing::BestPriceStrategy>());
        router.register_strategy(std::make_unique<routing::LiquiditySweepStrategy>());
        router.register_strategy(std::make_unique<routing::SmartIOCStrategy>());
        router.register_strategy(std::make_unique<routing::VWAPStrategy>());

        for (auto *ex : exchange_ptrs_)
        {
            routing::VenueScore vs;
            vs.venue_id = ex->venue_id();
            vs.latency_us = static_cast<double>(ex->avg_latency().count());
            vs.fill_rate = 0.95;
            vs.fee_rate = 0.001;
            vs.is_available = true;
            router.update_venue_score(ex->venue_id(), vs);
        }

        // Market data feeds (simulated mode for now)
        auto &md_aggregator = gateway_->market_data();
        const bool live_mode = (sys_config.market_data.provider != "simulated");
        const Symbol symbol("AAPL");
        const Price base_mid = to_price(150.0);

        if (live_mode)
        {
            live_provider_ = create_provider(sys_config.market_data);
            if (live_provider_)
            {
                live_provider_->set_aggregator(md_aggregator);
                constexpr VenueId alpaca_venue_id = 100;
                md_aggregator.register_venue(alpaca_venue_id);

                if (live_provider_->connect())
                {
                    for (const auto &sym : sys_config.market_data.symbols)
                        live_provider_->subscribe(Symbol(sym));
                }
            }
        }
        else
        {
            for (auto *ex : exchange_ptrs_)
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
                feeds_.push_back(std::move(feed));
            }

            for (auto &feed : feeds_)
                for (int i = 0; i < 10; ++i)
                    feed->generate_tick();
        }

        // Default watched symbol
        watched_symbols_.push_back(symbol);

        SOR_LOG_INFO("SorController backend initialized: {} venues, {} feeds",
                     exchange_ptrs_.size(), feeds_.size());
    }

    void SorController::wire_observers()
    {
        gateway_->set_fill_observer(
            [this](const Order &order, const ExecutionReport &report) {
                FillEvent ev;
                ev.order_id = order.id;
                ev.symbol = order.symbol;
                ev.side = order.side;
                ev.price = report.last_price;
                ev.quantity = report.last_quantity;
                ev.cum_quantity = report.cum_quantity;
                ev.leaves_quantity = report.leaves_quantity;
                ev.venue_id = report.venue_id;

                std::lock_guard<std::mutex> lock(event_mutex_);
                fill_events_.push_back(ev);
            });

        gateway_->set_completion_observer(
            [this](const Order &order) {
                CompletionEvent ev;
                ev.order_id = order.id;
                ev.symbol = order.symbol;
                ev.filled_quantity = order.filled_quantity;
                ev.avg_fill_price = order.avg_fill_price;

                std::lock_guard<std::mutex> lock(event_mutex_);
                completion_events_.push_back(ev);
            });
    }

    void SorController::start()
    {
        if (!gateway_) return;

        gateway_->start();
        threads_running_.store(true, std::memory_order_release);

        auto &md_aggregator = gateway_->market_data();

        // Market data feed thread
        md_feed_thread_ = std::thread([this, &md_aggregator]() {
            while (threads_running_.load(std::memory_order_relaxed))
            {
                for (auto &feed : feeds_)
                    feed->generate_tick();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        // Matching engine thread
        matching_thread_ = std::thread([this, &md_aggregator]() {
            Symbol symbol("AAPL");
            while (threads_running_.load(std::memory_order_relaxed))
            {
                auto nbbo = md_aggregator.get_nbbo(symbol);
                if (nbbo.valid())
                {
                    for (auto *ex : exchange_ptrs_)
                        ex->set_market_price(nbbo.best_bid, nbbo.best_ask);
                }
                for (auto *ex : exchange_ptrs_)
                    ex->process_matching();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });

        SOR_LOG_INFO("SorController started");
    }

    void SorController::stop()
    {
        if (!gateway_) return;

        threads_running_.store(false, std::memory_order_release);

        if (md_feed_thread_.joinable())
            md_feed_thread_.join();
        if (matching_thread_.joinable())
            matching_thread_.join();

        if (live_provider_)
            live_provider_->disconnect();

        for (auto &feed : feeds_)
            feed->stop();

        gateway_->stop();
        SOR_LOG_INFO("SorController stopped");
    }

    bool SorController::is_running() const
    {
        return gateway_ && gateway_->is_running();
    }

    // -- Order submission ────────────────────────────────────────────────────

    OrderId SorController::submit_order(const OrderParams &params)
    {
        if (!gateway_ || !gateway_->is_running())
            return INVALID_ORDER_ID;

        Order order{};
        order.id = gateway_->allocate_order_id();
        order.symbol = Symbol(params.symbol);
        order.side = params.side;
        order.quantity = params.quantity;
        order.remaining_quantity = params.quantity;
        order.type = params.type;
        order.price = to_price(params.price);
        order.tif = params.tif;
        order.strategy = params.strategy;
        order.state = OrderState::New;

        OrderId id = order.id;

        {
            std::lock_guard<std::mutex> lock(order_mutex_);
            tracked_orders_.push_back(id);
        }

        if (!gateway_->submit_order(std::move(order)))
            return INVALID_ORDER_ID;

        return id;
    }

    bool SorController::cancel_order(OrderId id)
    {
        if (!gateway_) return false;

        CancelRequest req{};
        req.order_id = id;
        return gateway_->cancel_order(std::move(req));
    }

    // -- Queries ─────────────────────────────────────────────────────────────

    std::optional<Order> SorController::get_order_snapshot(OrderId id) const
    {
        if (!gateway_) return std::nullopt;
        const Order *o = gateway_->get_order(id);
        if (!o) return std::nullopt;
        return *o;
    }

    std::vector<OrderId> SorController::get_children(OrderId parent_id) const
    {
        if (!gateway_) return {};
        return gateway_->execution_handler().get_children(parent_id);
    }

    market_data::NBBO SorController::get_nbbo(const Symbol &symbol) const
    {
        if (!gateway_) return {};
        return gateway_->market_data().get_nbbo(symbol);
    }

    market_data::AggregatedBook SorController::get_aggregated_book(const Symbol &symbol) const
    {
        if (!gateway_) return {};
        return gateway_->market_data().get_aggregated_book(symbol);
    }

    bool SorController::is_market_data_stale(const Symbol &symbol) const
    {
        if (!gateway_) return true;
        return gateway_->market_data().is_stale(symbol, std::chrono::microseconds(5'000'000));
    }

    gateway::Gateway::Stats SorController::get_gateway_stats() const
    {
        if (!gateway_) return {};
        return gateway_->get_stats();
    }

    execution::ExecutionHandler::Stats SorController::get_execution_stats() const
    {
        if (!gateway_) return {};
        return gateway_->execution_handler().get_stats();
    }

    risk::PositionInfo SorController::get_position(const Symbol &symbol) const
    {
        if (!gateway_) return {};
        return gateway_->risk_manager().get_position(symbol);
    }

    bool SorController::is_kill_switch_active() const
    {
        if (!gateway_) return false;
        return gateway_->risk_manager().is_kill_switch_active();
    }

    void SorController::toggle_kill_switch()
    {
        if (!gateway_) return;
        if (gateway_->risk_manager().is_kill_switch_active())
            gateway_->risk_manager().deactivate_kill_switch();
        else
            gateway_->risk_manager().activate_kill_switch();
    }

    // -- Event drain ────────────────────────────────────────────────────────

    std::vector<FillEvent> SorController::drain_fill_events()
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        std::vector<FillEvent> out;
        out.swap(fill_events_);
        return out;
    }

    std::vector<CompletionEvent> SorController::drain_completion_events()
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        std::vector<CompletionEvent> out;
        out.swap(completion_events_);
        return out;
    }

    std::vector<LogMessage> SorController::drain_log_messages()
    {
        std::lock_guard<std::mutex> lock(log_mutex_);
        std::vector<LogMessage> out(log_messages_.begin(), log_messages_.end());
        log_messages_.clear();
        return out;
    }

    // -- Symbol tracking ─────────────────────────────────────────────────────

    void SorController::add_watched_symbol(const Symbol &symbol)
    {
        for (const auto &s : watched_symbols_)
            if (s == symbol) return;
        watched_symbols_.push_back(symbol);
    }

    std::vector<OrderId> SorController::get_tracked_order_ids() const
    {
        std::lock_guard<std::mutex> lock(order_mutex_);
        return tracked_orders_;
    }

} // namespace sor::ui
