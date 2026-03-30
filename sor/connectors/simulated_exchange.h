#pragma once

// Simulated exchange with a simplified matching engine.
// Useful for integration testing, strategy back-testing, and development
// without requiring a live venue connection.

#include "connectors/venue_adapter.h"
#include <unordered_map>
#include <queue>
#include <mutex>
#include <random>
#include <chrono>
#include <string>

namespace sor::connectors
{

    class SimulatedExchange : public VenueAdapter
    {
    public:
        // -- Configuration ----------------------------------------------------------

        struct Config
        {
            VenueId venue_id{1};
            std::string name{"SimExchange"};
            std::chrono::microseconds latency{50};        // simulated processing latency
            std::chrono::microseconds latency_jitter{10}; // random jitter
            double fill_probability{0.95};                // probability of fill for marketable orders
            double partial_fill_probability{0.3};         // chance of partial fill
            double reject_probability{0.01};              // random reject rate
            double fee_rate{0.001};                       // 0.1% taker fee
        };

        explicit SimulatedExchange(Config config);

        // Non-copyable, non-movable (owns mutex + rng state).
        SimulatedExchange(const SimulatedExchange &) = delete;
        SimulatedExchange &operator=(const SimulatedExchange &) = delete;
        SimulatedExchange(SimulatedExchange &&) = delete;
        SimulatedExchange &operator=(SimulatedExchange &&) = delete;

        // -- VenueAdapter interface -------------------------------------------------

        bool connect() override;
        void disconnect() override;
        [[nodiscard]] bool is_connected() const override;

        bool send_order(const Order &order) override;
        bool cancel_order(const CancelRequest &request) override;

        [[nodiscard]] VenueId venue_id() const override;
        [[nodiscard]] const char *venue_name() const override;
        [[nodiscard]] VenueStatus status() const override;
        [[nodiscard]] std::chrono::microseconds avg_latency() const override;

        // -- Simulated exchange operations ------------------------------------------

        /// Process pending orders through the matching engine.
        /// Call this periodically (or from a dedicated thread) to simulate
        /// exchange-side matching.
        void process_matching();

        /// Set the simulated market price that the matching engine uses.
        void set_market_price(Price bid, Price ask);

        // -- Statistics --------------------------------------------------------------

        struct Stats
        {
            uint64_t orders_received{0};
            uint64_t orders_filled{0};
            uint64_t orders_partially_filled{0};
            uint64_t orders_rejected{0};
            uint64_t orders_canceled{0};
        };

        [[nodiscard]] Stats get_stats() const;

    private:
        // -- Internal order representation ------------------------------------------

        struct InternalOrder
        {
            Order order;
            Timestamp received_time;
            bool pending_cancel{false};
        };

        // -- Matching engine helpers -------------------------------------------------

        /// Attempt to match a single order.  Returns true if the order should
        /// remain in active_orders_ (i.e. it was re-queued for future matching).
        bool try_match(InternalOrder &internal);

        void generate_fill(InternalOrder &internal, Price fill_price, Quantity fill_qty);
        void generate_reject(const Order &order, const char *reason);
        void generate_cancel_ack(const Order &order);

        // -- Data members -----------------------------------------------------------

        Config config_;
        std::atomic<bool> connected_{false};

        // Active orders indexed by OrderId.
        std::unordered_map<OrderId, InternalOrder> active_orders_;

        // Orders waiting to be processed by the matching engine.
        std::queue<OrderId> pending_queue_;

        // Current simulated market prices.
        Price market_bid_{INVALID_PRICE};
        Price market_ask_{INVALID_PRICE};

        // PRNG for stochastic matching behaviour.
        std::mt19937 rng_;

        mutable std::mutex mutex_;
        Stats stats_;
        uint64_t next_exec_id_{1};

        // Latency tracking (updated under mutex_).
        std::chrono::microseconds total_latency_{0};
        uint64_t latency_samples_{0};
    };

} // namespace sor::connectors
