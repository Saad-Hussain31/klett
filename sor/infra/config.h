#pragma once

// YAML-based configuration with hot-reload support for the Smart Order Router.
// Thread-safe: get_config() returns a snapshot; file-change detection is
// poll-based (call check_for_changes() from a timer thread).

#include "core/types.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace sor::infra
{

    // ---------------------------------------------------------------------------
    // Per-venue configuration
    // ---------------------------------------------------------------------------

    struct VenueConfig
    {
        VenueId venue_id{0};
        std::string name;
        std::string type; // "simulated", "fix"
        std::string endpoint;
        double fee_rate{0.001};
        bool enabled{true};
        int32_t max_orders_per_second{100};
    };

    // ---------------------------------------------------------------------------
    // Strategy tuning knobs
    // ---------------------------------------------------------------------------

    struct StrategyConfig
    {
        RoutingStrategy default_strategy{RoutingStrategy::BestPrice};
        // VWAP parameters
        int32_t vwap_num_slices{10};
        int32_t vwap_duration_seconds{300};
        double vwap_urgency{0.5};
        // Sweep parameters
        Quantity sweep_min_slice{10};
        // IOC parameters
        int32_t ioc_slippage_ticks{2};
    };

    // ---------------------------------------------------------------------------
    // Risk limits
    // ---------------------------------------------------------------------------

    struct RiskConfig
    {
        Quantity max_order_quantity{10000};
        Price max_order_notional{0};
        Price max_position_notional{0};
        Quantity max_position_quantity{0};
        int32_t max_orders_per_second{100};
        int32_t max_open_orders{1000};
        Price max_loss{0};
    };

    // ---------------------------------------------------------------------------
    // Top-level system configuration
    // ---------------------------------------------------------------------------

    struct SystemConfig
    {
        std::string log_level{"info"};
        std::string log_file;
        bool enable_metrics{true};
        int32_t metrics_port{9090};
        std::vector<VenueConfig> venues;
        StrategyConfig strategy;
        RiskConfig risk;
        std::string data_dir;
    };

    // ---------------------------------------------------------------------------
    // ConfigManager -- singleton, thread-safe, supports hot reload
    // ---------------------------------------------------------------------------

    class ConfigManager
    {
    public:
        static ConfigManager &instance();

        /// Load configuration from a YAML file.
        /// Returns true on success.
        bool load(const std::string &path);

        /// Re-read the file that was passed to load().
        bool reload();

        /// Return a thread-safe snapshot of the current config.
        SystemConfig get_config() const;

        /// Register a callback invoked whenever the config changes.
        using ChangeCallback = std::function<void(const SystemConfig &)>;
        void on_change(ChangeCallback cb);

        /// Poll-based hot reload: compares file mtime and reloads if newer.
        /// Call this periodically from a background thread.
        void check_for_changes();

        // ---- Flat key-value access (dot-notation, e.g. "risk.max_loss") ------
        std::string get_string(const std::string &key, const std::string &default_val = "") const;
        int get_int(const std::string &key, int default_val = 0) const;
        double get_double(const std::string &key, double default_val = 0.0) const;
        bool get_bool(const std::string &key, bool default_val = false) const;

    private:
        ConfigManager() = default;
        ~ConfigManager() = default;
        ConfigManager(const ConfigManager &) = delete;
        ConfigManager &operator=(const ConfigManager &) = delete;

        /// Internal: parse a YAML file into config_ and flat_values_.
        bool parse_yaml(const std::string &path);

        /// Internal: flatten a YAML node tree into dot-notation key-value pairs.
        void flatten_node(const std::string &prefix,
                          const void *node); // YAML::Node passed as opaque ptr

        SystemConfig config_;
        std::string config_path_;
        std::chrono::system_clock::time_point last_modified_;
        std::vector<ChangeCallback> change_callbacks_;
        std::unordered_map<std::string, std::string> flat_values_;
        mutable std::mutex mutex_;
    };

} // namespace sor::infra
