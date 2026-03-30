#include "market_data/replay_engine.h"

#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <algorithm>

namespace sor::market_data
{

    // ---------------------------------------------------------------------------
    // CSV loading
    // ---------------------------------------------------------------------------

    bool ReplayEngine::load_from_csv(const std::string &path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            return false;
        }

        std::string line;
        // Skip header line if present.
        if (!std::getline(file, line))
        {
            return false;
        }

        // Detect header: if the first field is not a number, it is a header.
        bool has_header = false;
        if (!line.empty() && !std::isdigit(static_cast<unsigned char>(line[0])) && line[0] != '-')
        {
            has_header = true;
        }

        // If no header, rewind and process from beginning.
        if (!has_header)
        {
            file.clear();
            file.seekg(0);
        }

        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
                continue; // Skip comments.

            std::istringstream ss(line);
            std::string token;
            std::vector<std::string> fields;

            while (std::getline(ss, token, ','))
            {
                // Trim whitespace.
                auto start = token.find_first_not_of(" \t");
                auto end = token.find_last_not_of(" \t");
                if (start != std::string::npos)
                {
                    fields.push_back(token.substr(start, end - start + 1));
                }
                else
                {
                    fields.push_back("");
                }
            }

            // Minimum required: timestamp, symbol, venue, bid, bid_qty, ask, ask_qty
            if (fields.size() < 7)
                continue;

            MarketDataTick tick{};

            // Parse timestamp (microseconds since epoch, stored as steady_clock offset).
            int64_t ts_us = std::strtoll(fields[0].c_str(), nullptr, 10);
            tick.timestamp = Timestamp{} + std::chrono::microseconds(ts_us);

            // Symbol.
            tick.symbol = Symbol(fields[1]);

            // Venue ID.
            tick.venue_id = static_cast<VenueId>(std::strtoul(fields[2].c_str(), nullptr, 10));

            // Bid/ask prices (as doubles, converted to fixed-point).
            tick.bid_price = to_price(std::strtod(fields[3].c_str(), nullptr));
            tick.bid_qty = static_cast<Quantity>(std::strtoll(fields[4].c_str(), nullptr, 10));
            tick.ask_price = to_price(std::strtod(fields[5].c_str(), nullptr));
            tick.ask_qty = static_cast<Quantity>(std::strtoll(fields[6].c_str(), nullptr, 10));

            // Optional L2 depth: pairs of (bid_price, bid_qty, ask_price, ask_qty).
            size_t extra = fields.size() - 7;
            size_t l2_levels = extra / 4;
            l2_levels = std::min(l2_levels, MAX_DEPTH);

            for (size_t i = 0; i < l2_levels; ++i)
            {
                size_t base = 7 + i * 4;
                tick.bid_levels[i].price = to_price(std::strtod(fields[base].c_str(), nullptr));
                tick.bid_levels[i].qty = static_cast<Quantity>(std::strtoll(fields[base + 1].c_str(), nullptr, 10));
                tick.ask_levels[i].price = to_price(std::strtod(fields[base + 2].c_str(), nullptr));
                tick.ask_levels[i].qty = static_cast<Quantity>(std::strtoll(fields[base + 3].c_str(), nullptr, 10));
            }
            tick.bid_depth = l2_levels;
            tick.ask_depth = l2_levels;

            ticks_.push_back(tick);
        }

        return !ticks_.empty();
    }

    // ---------------------------------------------------------------------------
    // Binary loading / saving
    // ---------------------------------------------------------------------------

    bool ReplayEngine::load_from_binary(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        // File format: uint64_t count, then count MarketDataTick structs.
        uint64_t count = 0;
        file.read(reinterpret_cast<char *>(&count), sizeof(count));
        if (!file || count == 0)
        {
            return false;
        }

        // Sanity limit to prevent absurd allocations.
        if (count > 100'000'000ULL)
        {
            return false;
        }

        ticks_.resize(static_cast<size_t>(count));
        file.read(reinterpret_cast<char *>(ticks_.data()),
                  static_cast<std::streamsize>(count * sizeof(MarketDataTick)));

        return file.good();
    }

    bool ReplayEngine::save_to_binary(const std::string &path) const
    {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        uint64_t count = ticks_.size();
        file.write(reinterpret_cast<const char *>(&count), sizeof(count));
        file.write(reinterpret_cast<const char *>(ticks_.data()),
                   static_cast<std::streamsize>(count * sizeof(MarketDataTick)));

        return file.good();
    }

    // ---------------------------------------------------------------------------
    // Tick management
    // ---------------------------------------------------------------------------

    void ReplayEngine::add_tick(MarketDataTick tick)
    {
        ticks_.push_back(std::move(tick));
    }

    // ---------------------------------------------------------------------------
    // Synthetic data generation
    // ---------------------------------------------------------------------------

    void ReplayEngine::generate_synthetic(const Symbol &symbol, VenueId venue_id,
                                          Price start_mid, size_t num_ticks,
                                          std::chrono::microseconds interval)
    {
        // xorshift64 seeded from start_mid for determinism.
        uint64_t rng = static_cast<uint64_t>(start_mid) ^ 0xBEEFCAFE12345678ULL;
        auto xorshift = [&rng]() -> uint64_t
        {
            uint64_t x = rng;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            rng = x;
            return x;
        };

        Price mid = start_mid;
        const Price tick_size = to_price(0.01);
        Timestamp ts = Timestamp{} + std::chrono::microseconds(1000000); // Start at t=1s.

        ticks_.reserve(ticks_.size() + num_ticks);

        for (size_t i = 0; i < num_ticks; ++i)
        {
            // Random walk: -1, 0, or +1 tick.
            int64_t step = static_cast<int64_t>(xorshift() % 3) - 1;
            mid += step * tick_size;
            if (mid < tick_size)
                mid = tick_size;

            // Spread: 1-3 ticks.
            Price half_spread = static_cast<Price>(1 + xorshift() % 2) * tick_size;

            MarketDataTick tick{};
            tick.symbol = symbol;
            tick.venue_id = venue_id;
            tick.timestamp = ts;
            tick.bid_price = mid - half_spread;
            tick.ask_price = mid + half_spread;
            tick.bid_qty = static_cast<Quantity>(50 + xorshift() % 200);
            tick.ask_qty = static_cast<Quantity>(50 + xorshift() % 200);

            if (tick.bid_price <= 0)
                tick.bid_price = tick_size;

            ticks_.push_back(tick);
            ts += interval;
        }
    }

    // ---------------------------------------------------------------------------
    // Replay control
    // ---------------------------------------------------------------------------

    void ReplayEngine::start(TickCallback cb)
    {
        callback_ = std::move(cb);
        running_.store(true, std::memory_order_release);
        paused_.store(false, std::memory_order_release);

        // Replay all remaining ticks respecting timing.
        while (running_.load(std::memory_order_acquire) && current_pos_ < ticks_.size())
        {
            // Honor pause.
            while (paused_.load(std::memory_order_acquire) &&
                   running_.load(std::memory_order_acquire))
            {
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }

            if (!running_.load(std::memory_order_acquire))
                break;

            // Apply inter-tick delay if speed_multiplier > 0 and not the first tick.
            if (speed_multiplier_ > 0.0 && current_pos_ > 0 &&
                current_pos_ < ticks_.size())
            {
                const auto &prev = ticks_[current_pos_ - 1];
                const auto &curr = ticks_[current_pos_];
                auto delta = curr.timestamp - prev.timestamp;
                if (delta.count() > 0)
                {
                    auto scaled = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::duration<double, std::micro>(
                            static_cast<double>(
                                std::chrono::duration_cast<std::chrono::microseconds>(delta).count()) /
                            speed_multiplier_));
                    if (scaled.count() > 0)
                    {
                        std::this_thread::sleep_for(scaled);
                    }
                }
            }

            if (callback_)
            {
                callback_(ticks_[current_pos_]);
            }
            ++current_pos_;
        }

        running_.store(false, std::memory_order_release);
    }

    void ReplayEngine::stop()
    {
        running_.store(false, std::memory_order_release);
        paused_.store(false, std::memory_order_release);
    }

    void ReplayEngine::pause()
    {
        paused_.store(true, std::memory_order_release);
    }

    void ReplayEngine::resume()
    {
        paused_.store(false, std::memory_order_release);
    }

    void ReplayEngine::reset()
    {
        stop();
        current_pos_ = 0;
    }

    bool ReplayEngine::step()
    {
        if (current_pos_ >= ticks_.size())
        {
            return false;
        }

        if (callback_)
        {
            callback_(ticks_[current_pos_]);
        }
        ++current_pos_;
        return current_pos_ < ticks_.size();
    }

    void ReplayEngine::set_speed_multiplier(double multiplier)
    {
        speed_multiplier_ = multiplier;
    }

} // namespace sor::market_data
