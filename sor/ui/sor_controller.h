#pragma once

#include "ui/order_params.h"
#include "core/types.h"
#include "core/order.h"
#include "market_data/aggregator.h"
#include "execution/execution_handler.h"
#include "execution/fill_manager.h"
#include "gateway/gateway.h"
#include "risk/risk_manager.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace sor::connectors { class SimulatedExchange; }
namespace sor::market_data { class SimulatedFeedHandler; class MarketDataProvider; }
namespace spdlog { namespace sinks { template<typename Mutex> class base_sink; } }

namespace sor::ui
{

    // Events pushed by observer callbacks, drained once per UI frame.
    struct FillEvent
    {
        OrderId order_id;
        Symbol symbol;
        Side side;
        Price price;
        Quantity quantity;
        Quantity cum_quantity;
        Quantity leaves_quantity;
        VenueId venue_id;
    };

    struct CompletionEvent
    {
        OrderId order_id;
        Symbol symbol;
        Quantity filled_quantity;
        Price avg_fill_price;
    };

    struct LogMessage
    {
        std::string text;
        int level; // spdlog::level::level_enum
    };

    class SorController
    {
    public:
        SorController();
        ~SorController();

        SorController(const SorController &) = delete;
        SorController &operator=(const SorController &) = delete;

        // -- Lifecycle -------------------------------------------------------

        bool initialize(const std::string &config_path = "");
        void start();
        void stop();
        [[nodiscard]] bool is_running() const;

        // -- Order submission ------------------------------------------------

        OrderId submit_order(const OrderParams &params);
        bool cancel_order(OrderId id);

        // -- Queries (thread-safe, returns copies) ---------------------------

        std::optional<Order> get_order_snapshot(OrderId id) const;
        std::vector<OrderId> get_children(OrderId parent_id) const;
        market_data::NBBO get_nbbo(const Symbol &symbol) const;
        market_data::AggregatedBook get_aggregated_book(const Symbol &symbol) const;
        bool is_market_data_stale(const Symbol &symbol) const;

        // -- Stats -----------------------------------------------------------

        gateway::Gateway::Stats get_gateway_stats() const;
        execution::ExecutionHandler::Stats get_execution_stats() const;
        risk::PositionInfo get_position(const Symbol &symbol) const;
        bool is_kill_switch_active() const;
        void toggle_kill_switch();

        // -- Event drain (called once per UI frame) --------------------------

        std::vector<FillEvent> drain_fill_events();
        std::vector<CompletionEvent> drain_completion_events();
        std::vector<LogMessage> drain_log_messages();

        // -- Symbol tracking -------------------------------------------------

        const std::vector<Symbol> &watched_symbols() const { return watched_symbols_; }
        void add_watched_symbol(const Symbol &symbol);
        std::vector<OrderId> get_tracked_order_ids() const;

    private:
        void setup_backend(const std::string &config_path);
        void wire_observers();

        std::unique_ptr<gateway::Gateway> gateway_;

        // Raw pointers to exchanges owned by Gateway (for matching thread).
        std::vector<connectors::SimulatedExchange *> exchange_ptrs_;

        // Simulated feed handlers (owned).
        std::vector<std::unique_ptr<market_data::SimulatedFeedHandler>> feeds_;

        // Live provider (owned, null in simulated mode).
        std::unique_ptr<market_data::MarketDataProvider> live_provider_;

        // Background threads.
        std::thread md_feed_thread_;
        std::thread matching_thread_;
        std::atomic<bool> threads_running_{false};

        // Event buffers (mutex-protected, drained per frame).
        std::vector<FillEvent> fill_events_;
        std::vector<CompletionEvent> completion_events_;
        mutable std::mutex event_mutex_;

        // Log sink buffer.
        std::deque<LogMessage> log_messages_;
        mutable std::mutex log_mutex_;
        std::shared_ptr<spdlog::sinks::base_sink<std::mutex>> log_sink_;

        // Tracked order IDs (in submission order).
        std::vector<OrderId> tracked_orders_;
        mutable std::mutex order_mutex_;

        // Symbols for market data panel.
        std::vector<Symbol> watched_symbols_;
    };

} // namespace sor::ui
