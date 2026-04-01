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
    // Gateway configuration
    // ---------------------------------------------------------------------------

    struct GatewayConfig
    {
        struct Fix
        {
            bool enabled{false};
            int32_t listen_port{9876};
            std::string sender_comp_id{"SOR"};
            std::string target_comp_id{"CLIENT"};
            int32_t heartbeat_interval_sec{30};
        } fix;

        struct Api
        {
            bool enabled{true};
            std::string zmq_order_endpoint{"tcp://*:5555"};
            std::string zmq_market_data_endpoint{"tcp://*:5556"};
            std::string zmq_execution_endpoint{"tcp://*:5557"};
            int32_t max_message_size{65536};
        } api;
    };

    // ---------------------------------------------------------------------------
    // Metrics configuration
    // ---------------------------------------------------------------------------

    struct MetricsConfig
    {
        bool enabled{true};
        std::string bind_address{"0.0.0.0"};
        int32_t port{9090};
        std::string path{"/metrics"};
    };

    // ---------------------------------------------------------------------------
    // Market data provider configuration
    // ---------------------------------------------------------------------------

    struct MarketDataConfig
    {
        std::string provider{"simulated"}; // "simulated", "alpaca", "replay"

        // Alpaca-specific
        std::string alpaca_api_key;
        std::string alpaca_api_secret;
        std::string alpaca_ws_url{"wss://stream.data.alpaca.markets/v2/iex"};
        bool alpaca_use_sip{false};

        // Symbols to subscribe
        std::vector<std::string> symbols;

        // Feed quality
        int32_t staleness_threshold_ms{5000};

        // Reconnection
        int32_t reconnect_delay_sec{5};
        int32_t max_reconnect_attempts{10};
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
        GatewayConfig gateway;
        MetricsConfig metrics;
        MarketDataConfig market_data;
        std::string data_dir;
    };

    // ---------------------------------------------------------------------------
    // Config validation -- returns empty string on success, error message on
    // failure. Call after loading to fail fast on invalid configuration.
    // ---------------------------------------------------------------------------

    std::string validate_config(const SystemConfig &config);

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
