#pragma once

// Historical market data replay engine.
// Supports loading from CSV, binary, or programmatic generation.
// Replay supports speed control, pause/resume, and single-step mode.

#include "core/types.h"
#include "market_data/book.h"
#include <vector>
#include <string>
#include <functional>
#include <atomic>
#include <chrono>

namespace sor::market_data
{

    struct MarketDataTick
    {
        Symbol symbol;
        VenueId venue_id{0};
        Timestamp timestamp{};
        Price bid_price{INVALID_PRICE};
        Quantity bid_qty{0};
        Price ask_price{INVALID_PRICE};
        Quantity ask_qty{0};

        // Optional L2 depth.
        struct Level
        {
            Price price{INVALID_PRICE};
            Quantity qty{0};
        };
        std::array<Level, MAX_DEPTH> bid_levels{};
        std::array<Level, MAX_DEPTH> ask_levels{};
        size_t bid_depth{0};
        size_t ask_depth{0};
    };

    class ReplayEngine
    {
    public:
        using TickCallback = std::function<void(const MarketDataTick &)>;

        // Load ticks from CSV file.
        // Expected format: timestamp_us,symbol,venue_id,bid,bid_qty,ask,ask_qty
        // Optional additional columns for L2 depth (bid1,bidqty1,ask1,askqty1,...).
        bool load_from_csv(const std::string &path);

        // Load ticks from binary file (raw MarketDataTick structs).
        bool load_from_binary(const std::string &path);

        // Save ticks to binary file.
        bool save_to_binary(const std::string &path) const;

        // Add tick programmatically.
        void add_tick(MarketDataTick tick);

        // Generate synthetic random-walk data for testing.
        void generate_synthetic(const Symbol &symbol, VenueId venue_id,
                                Price start_mid, size_t num_ticks,
                                std::chrono::microseconds interval);

        // Replay control.
        void start(TickCallback cb);
        void stop();
        void pause();
        void resume();
        void reset();

        // Step through one tick at a time. Returns false when no more ticks.
        bool step();

        // Speed control (1.0 = real-time, 2.0 = 2x speed, 0.0 = as fast as possible).
        void set_speed_multiplier(double multiplier);

        [[nodiscard]] size_t total_ticks() const noexcept { return ticks_.size(); }
        [[nodiscard]] size_t current_position() const noexcept { return current_pos_; }
        [[nodiscard]] bool is_running() const noexcept { return running_.load(std::memory_order_acquire); }
        [[nodiscard]] bool is_paused() const noexcept { return paused_.load(std::memory_order_acquire); }

        // Direct access for inspection.
        [[nodiscard]] const std::vector<MarketDataTick> &ticks() const noexcept { return ticks_; }

    private:
        std::vector<MarketDataTick> ticks_;
        size_t current_pos_{0};
        TickCallback callback_;
        std::atomic<bool> running_{false};
        std::atomic<bool> paused_{false};
        double speed_multiplier_{1.0};
    };

} // namespace sor::market_data
