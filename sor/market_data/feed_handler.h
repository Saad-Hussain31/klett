#pragma once

// Feed handler interface and simulated feed for testing.
// FeedHandler provides the abstract interface for receiving market data.
// SimulatedFeedHandler generates deterministic pseudo-random book updates
// using xorshift64, suitable for backtesting and integration tests.

#include "core/types.h"
#include "market_data/book.h"
#include <functional>
#include <atomic>
#include <chrono>

namespace sor::market_data
{

    class FeedHandler
    {
    public:
        using BookUpdateCallback = std::function<void(VenueId, const Symbol &, const OrderBook &)>;

        virtual ~FeedHandler() = default;
        virtual void start() = 0;
        virtual void stop() = 0;
        [[nodiscard]] virtual bool is_running() const = 0;

        void set_book_callback(BookUpdateCallback cb) { callback_ = std::move(cb); }

    protected:
        BookUpdateCallback callback_;
    };

    // Simulated feed that generates deterministic pseudo-random market data updates.
    class SimulatedFeedHandler : public FeedHandler
    {
    public:
        struct Config
        {
            Symbol symbol;
            VenueId venue_id{0};
            Price initial_mid_price{to_price(100.0)};
            Price tick_size{to_price(0.01)};
            int32_t max_depth{10};
            Quantity base_quantity{100};
            std::chrono::microseconds update_interval{1000}; // 1ms default
            double volatility{0.001};                        // price movement per tick as fraction
            uint64_t rng_seed{0x12345678DEADBEEFULL};        // deterministic seed
        };

        explicit SimulatedFeedHandler(Config config);

        void start() override;
        void stop() override;
        [[nodiscard]] bool is_running() const override;

        // Generate one tick (call in a loop or timer).
        void generate_tick();

        // Access current book state.
        [[nodiscard]] const OrderBook &current_book() const noexcept { return book_; }

    private:
        Config config_;
        OrderBook book_;
        std::atomic<bool> running_{false};
        Price current_mid_;
        uint64_t sequence_{0};
        uint64_t rng_state_;

        uint64_t next_random();
        Price random_walk();
        void rebuild_book();
    };

} // namespace sor::market_data
