#pragma once

// Gateway -- the top-level entry point for the Smart Order Router.
//
// Owns the order processing loop, the execution report processing loop,
// and all subsystem references.  External callers submit orders and
// cancel requests through thread-safe MPSC queues.  Internal processing
// happens on dedicated threads with lock-free queue draining.

#include "core/types.h"
#include "core/order.h"
#include "core/spsc_queue.h"
#include "core/mpsc_queue.h"
#include "market_data/aggregator.h"
#include "risk/risk_manager.h"
#include "routing/engine.h"
#include "execution/execution_handler.h"
#include "execution/fill_manager.h"
#include "connectors/venue_adapter.h"
#include <memory>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <vector>

namespace sor::gateway
{

    class Gateway
    {
    public:
        struct Config
        {
            size_t order_queue_size{8192};
            size_t report_queue_size{8192};
            bool enable_metrics{true};
            std::string config_path;
        };

        explicit Gateway(Config config);
        ~Gateway();

        // Non-copyable, non-movable.
        Gateway(const Gateway &) = delete;
        Gateway &operator=(const Gateway &) = delete;
        Gateway(Gateway &&) = delete;
        Gateway &operator=(Gateway &&) = delete;

        // -- Lifecycle ------------------------------------------------------------

        bool initialize();
        void start();
        void stop();
        [[nodiscard]] bool is_running() const;

        // -- Order submission (thread-safe, non-blocking) -------------------------

        bool submit_order(Order order);
        bool cancel_order(CancelRequest request);

        // -- Venue management -----------------------------------------------------

        void add_venue(std::unique_ptr<connectors::VenueAdapter> adapter);

        // -- Subsystem accessors --------------------------------------------------

        market_data::MarketDataAggregator &market_data() { return md_aggregator_; }
        risk::RiskManager &risk_manager() { return risk_manager_; }
        routing::RoutingEngine &routing_engine() { return *routing_engine_; }
        execution::ExecutionHandler &execution_handler() { return exec_handler_; }
        execution::FillManager &fill_manager() { return fill_manager_; }

        // -- Order query ----------------------------------------------------------

        const Order *get_order(OrderId id) const;

        // -- Statistics -----------------------------------------------------------

        struct Stats
        {
            uint64_t orders_submitted{0};
            uint64_t orders_routed{0};
            uint64_t orders_completed{0};
            uint64_t orders_rejected{0};
        };
        Stats get_stats() const;

    private:
        void order_processing_loop();
        void execution_processing_loop();
        void process_single_order(Order &order);
        void on_execution_report(const ExecutionReport &report);
        OrderId generate_order_id();

        Config config_;
        std::atomic<bool> running_{false};

        // Queues.
        MPSCQueue<Order, 8192> order_queue_;
        MPSCQueue<CancelRequest, 4096> cancel_queue_;
        SPSCQueue<ExecutionReport, 8192> report_queue_;

        // Subsystems.
        market_data::MarketDataAggregator md_aggregator_;
        risk::RiskManager risk_manager_;
        std::unique_ptr<routing::RoutingEngine> routing_engine_;
        execution::ExecutionHandler exec_handler_;
        execution::FillManager fill_manager_;

        // Venues.
        std::unordered_map<VenueId, std::unique_ptr<connectors::VenueAdapter>> venues_;

        // Processing threads.
        std::thread order_thread_;
        std::thread exec_thread_;

        // Monotonic order ID generator.
        std::atomic<uint64_t> next_order_id_{1};

        Stats stats_;
    };

} // namespace sor::gateway
