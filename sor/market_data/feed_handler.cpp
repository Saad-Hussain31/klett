#include "market_data/feed_handler.h"

#include <cmath>

namespace sor::market_data
{

    // ---------------------------------------------------------------------------
    // SimulatedFeedHandler
    // ---------------------------------------------------------------------------

    SimulatedFeedHandler::SimulatedFeedHandler(Config config)
        : config_(std::move(config)), current_mid_(config_.initial_mid_price), rng_state_(config_.rng_seed)
    {
        book_.symbol = config_.symbol;
        book_.venue_id = config_.venue_id;
    }

    void SimulatedFeedHandler::start()
    {
        running_.store(true, std::memory_order_release);
    }

    void SimulatedFeedHandler::stop()
    {
        running_.store(false, std::memory_order_release);
    }

    bool SimulatedFeedHandler::is_running() const
    {
        return running_.load(std::memory_order_acquire);
    }

    void SimulatedFeedHandler::generate_tick()
    {
        if (!running_.load(std::memory_order_acquire))
        {
            return;
        }

        // Advance the mid price via random walk.
        current_mid_ = random_walk();

        // Clamp to positive territory.
        if (current_mid_ < config_.tick_size)
        {
            current_mid_ = config_.tick_size;
        }

        // Rebuild the full book around the new mid.
        rebuild_book();

        // Update metadata.
        book_.last_update = std::chrono::steady_clock::now();
        book_.sequence = ++sequence_;

        // Notify subscriber.
        if (callback_)
        {
            callback_(config_.venue_id, config_.symbol, book_);
        }
    }

    uint64_t SimulatedFeedHandler::next_random()
    {
        // xorshift64 -- simple, fast, deterministic.
        uint64_t x = rng_state_;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rng_state_ = x;
        return x;
    }

    Price SimulatedFeedHandler::random_walk()
    {
        // Generate a signed step: +1 or -1 tick, weighted by volatility.
        uint64_t r = next_random();

        // Use volatility to scale the step magnitude.
        // Map the RNG output to a step in ticks: [-max_step, +max_step].
        const int64_t max_step_ticks = std::max(
            int64_t{1},
            static_cast<int64_t>(config_.volatility * to_double(current_mid_) / to_double(config_.tick_size)));

        // Map r to range [-max_step_ticks, +max_step_ticks].
        int64_t step = static_cast<int64_t>(r % static_cast<uint64_t>(2 * max_step_ticks + 1)) - max_step_ticks;

        return current_mid_ + step * config_.tick_size;
    }

    void SimulatedFeedHandler::rebuild_book()
    {
        book_.bids.clear();
        book_.asks.clear();

        // Spread = 2-4 ticks (randomly chosen).
        uint64_t spread_ticks = 2 + (next_random() % 3); // 2, 3, or 4
        Price half_spread = static_cast<Price>(spread_ticks / 2) * config_.tick_size;
        // Ensure at least 1 tick half-spread.
        if (half_spread < config_.tick_size)
        {
            half_spread = config_.tick_size;
        }

        Price best_bid_price = current_mid_ - half_spread;
        Price best_ask_price = current_mid_ + half_spread;

        // Ensure spread is at least 1 tick.
        if (best_ask_price <= best_bid_price)
        {
            best_ask_price = best_bid_price + config_.tick_size;
        }

        const int32_t depth = std::min(config_.max_depth, static_cast<int32_t>(MAX_DEPTH));

        // Build bid levels (descending from best_bid_price).
        for (int32_t i = 0; i < depth; ++i)
        {
            Price price = best_bid_price - static_cast<Price>(i) * config_.tick_size;
            if (price <= 0)
                break;

            // Random quantity: base_quantity * [0.5, 2.0).
            uint64_t r = next_random();
            Quantity qty = config_.base_quantity / 2 + static_cast<Quantity>(r % static_cast<uint64_t>(config_.base_quantity * 3 / 2 + 1));
            if (qty <= 0)
                qty = 1;

            int32_t order_count = static_cast<int32_t>(1 + (next_random() % 10));

            PriceLevel level{price, qty, order_count};
            book_.bids.insert_sorted(level, /*is_bid=*/true);
        }

        // Build ask levels (ascending from best_ask_price).
        for (int32_t i = 0; i < depth; ++i)
        {
            Price price = best_ask_price + static_cast<Price>(i) * config_.tick_size;

            uint64_t r = next_random();
            Quantity qty = config_.base_quantity / 2 + static_cast<Quantity>(r % static_cast<uint64_t>(config_.base_quantity * 3 / 2 + 1));
            if (qty <= 0)
                qty = 1;

            int32_t order_count = static_cast<int32_t>(1 + (next_random() % 10));

            PriceLevel level{price, qty, order_count};
            book_.asks.insert_sorted(level, /*is_bid=*/false);
        }
    }

} // namespace sor::market_data
