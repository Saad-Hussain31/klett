#include "infra/config.h"
#include "infra/logging.h"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <stdexcept>

namespace sor::infra
{

    // ---------------------------------------------------------------------------
    // Portable file_time_type -> system_clock conversion (pre-C++20 clock_cast)
    // ---------------------------------------------------------------------------

    namespace
    {
        std::chrono::system_clock::time_point to_sys_time(
            std::filesystem::file_time_type ftime)
        {
            // Compute the delta between file_clock::now() and system_clock::now()
            // at approximately the same instant, then apply it.
            const auto file_now = std::filesystem::file_time_type::clock::now();
            const auto sys_now = std::chrono::system_clock::now();
            const auto delta = ftime - file_now;
            return sys_now + std::chrono::duration_cast<std::chrono::system_clock::duration>(delta);
        }
    } // anonymous namespace

    // ---------------------------------------------------------------------------
    // Singleton
    // ---------------------------------------------------------------------------

    ConfigManager &ConfigManager::instance()
    {
        static ConfigManager inst;
        return inst;
    }

    // ---------------------------------------------------------------------------
    // Public API
    // ---------------------------------------------------------------------------

    bool ConfigManager::load(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_path_ = path;
        if (!parse_yaml(path))
        {
            return false;
        }

        // Record initial mtime for hot-reload detection.
        try
        {
            const auto ftime = std::filesystem::last_write_time(path);
            last_modified_ = to_sys_time(ftime);
        }
        catch (...)
        {
            last_modified_ = std::chrono::system_clock::now();
        }

        return true;
    }

    bool ConfigManager::reload()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (config_path_.empty())
        {
            SOR_LOG_ERROR("ConfigManager::reload() called but no config file loaded");
            return false;
        }

        SystemConfig old_config = config_;
        if (!parse_yaml(config_path_))
        {
            SOR_LOG_ERROR("Config reload failed, keeping previous configuration");
            config_ = old_config;
            return false;
        }

        SOR_LOG_INFO("Configuration reloaded from {}", config_path_);

        // Notify all registered change listeners (still under lock -- listeners
        // must not call back into ConfigManager).
        for (const auto &cb : change_callbacks_)
        {
            try
            {
                cb(config_);
            }
            catch (const std::exception &e)
            {
                SOR_LOG_ERROR("Config change callback threw: {}", e.what());
            }
        }
        return true;
    }

    SystemConfig ConfigManager::get_config() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return config_;
    }

    void ConfigManager::on_change(ChangeCallback cb)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        change_callbacks_.push_back(std::move(cb));
    }

    void ConfigManager::check_for_changes()
    {
        if (config_path_.empty())
        {
            return;
        }

        try
        {
            const auto ftime = std::filesystem::last_write_time(config_path_);
            const auto sys_ftime = to_sys_time(ftime);

            std::chrono::system_clock::time_point cached;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                cached = last_modified_;
            }

            if (sys_ftime > cached)
            {
                SOR_LOG_INFO("Config file changed on disk, reloading: {}", config_path_);
                reload();

                std::lock_guard<std::mutex> lock(mutex_);
                last_modified_ = sys_ftime;
            }
        }
        catch (const std::exception &e)
        {
            SOR_LOG_WARN("Failed to check config file mtime: {}", e.what());
        }
    }

    // ---------------------------------------------------------------------------
    // Flat key-value accessors
    // ---------------------------------------------------------------------------

    std::string ConfigManager::get_string(const std::string &key,
                                          const std::string &default_val) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flat_values_.find(key);
        return (it != flat_values_.end()) ? it->second : default_val;
    }

    int ConfigManager::get_int(const std::string &key, int default_val) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flat_values_.find(key);
        if (it == flat_values_.end())
            return default_val;
        try
        {
            return std::stoi(it->second);
        }
        catch (...)
        {
            return default_val;
        }
    }

    double ConfigManager::get_double(const std::string &key, double default_val) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flat_values_.find(key);
        if (it == flat_values_.end())
            return default_val;
        try
        {
            return std::stod(it->second);
        }
        catch (...)
        {
            return default_val;
        }
    }

    bool ConfigManager::get_bool(const std::string &key, bool default_val) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = flat_values_.find(key);
        if (it == flat_values_.end())
            return default_val;
        const auto &v = it->second;
        return (v == "true" || v == "1" || v == "yes" || v == "on");
    }

    // ---------------------------------------------------------------------------
    // YAML parsing internals
    // ---------------------------------------------------------------------------

    namespace
    {

        RoutingStrategy parse_routing_strategy(const std::string &s)
        {
            if (s == "best_price" || s == "BestPrice")
                return RoutingStrategy::BestPrice;
            if (s == "liquidity_sweep" || s == "LiquiditySweep")
                return RoutingStrategy::LiquiditySweep;
            if (s == "smart_ioc" || s == "SmartIOC")
                return RoutingStrategy::SmartIOC;
            if (s == "vwap" || s == "VWAP")
                return RoutingStrategy::VWAP;
            return RoutingStrategy::BestPrice;
        }

        /// Recursively flatten a YAML tree into dot-notation key-value pairs.
        void flatten_yaml(const std::string &prefix,
                          const YAML::Node &node,
                          std::unordered_map<std::string, std::string> &out)
        {
            if (node.IsScalar())
            {
                out[prefix] = node.Scalar();
                return;
            }
            if (node.IsMap())
            {
                for (auto it = node.begin(); it != node.end(); ++it)
                {
                    const std::string key = it->first.as<std::string>();
                    const std::string child_prefix = prefix.empty() ? key : (prefix + "." + key);
                    flatten_yaml(child_prefix, it->second, out);
                }
                return;
            }
            if (node.IsSequence())
            {
                for (std::size_t i = 0; i < node.size(); ++i)
                {
                    const std::string child_prefix = prefix + "[" + std::to_string(i) + "]";
                    flatten_yaml(child_prefix, node[i], out);
                }
            }
        }

    } // anonymous namespace

    void ConfigManager::flatten_node(const std::string &prefix, const void *node)
    {
        // Delegate to the free function that has access to YAML::Node type.
        flatten_yaml(prefix, *static_cast<const YAML::Node *>(node), flat_values_);
    }

    bool ConfigManager::parse_yaml(const std::string &path)
    {
        YAML::Node root;
        try
        {
            root = YAML::LoadFile(path);
        }
        catch (const YAML::Exception &e)
        {
            SOR_LOG_ERROR("Failed to parse YAML config '{}': {}", path, e.what());
            return false;
        }

        // -- Flatten for generic key-value access --
        flat_values_.clear();
        flatten_yaml("", root, flat_values_);

        // -- Structured parse --
        SystemConfig cfg;

        // System-level fields (supports both flat and nested under "system:" key)
        auto sys = root["system"];
        if (sys && sys.IsMap())
        {
            if (sys["log_level"])
                cfg.log_level = sys["log_level"].as<std::string>("info");
            if (sys["log_file"])
                cfg.log_file = sys["log_file"].as<std::string>("");
        }
        // Flat keys override
        if (root["log_level"])
            cfg.log_level = root["log_level"].as<std::string>("info");
        if (root["log_file"])
            cfg.log_file = root["log_file"].as<std::string>("");
        if (root["data_dir"])
            cfg.data_dir = root["data_dir"].as<std::string>("");

        // -- Venues --
        // Supports both field-name styles:
        //   YAML style:   id, adapter, max_order_rate
        //   Struct style:  venue_id, type, max_orders_per_second
        if (root["venues"] && root["venues"].IsSequence())
        {
            for (const auto &vnode : root["venues"])
            {
                VenueConfig vc;
                vc.venue_id = vnode["id"].as<VenueId>(
                    vnode["venue_id"].as<VenueId>(0));
                vc.name = vnode["name"].as<std::string>("");
                vc.type = vnode["adapter"].as<std::string>(
                    vnode["type"].as<std::string>("simulated"));
                vc.endpoint = vnode["endpoint"].as<std::string>("");
                vc.fee_rate = vnode["fee_rate"].as<double>(0.001);
                vc.enabled = vnode["enabled"].as<bool>(true);
                vc.max_orders_per_second = vnode["max_order_rate"].as<int32_t>(
                    vnode["max_orders_per_second"].as<int32_t>(100));
                cfg.venues.push_back(std::move(vc));
            }
        }

        // -- Strategy --
        // Supports both "strategy" (flat) and "routing" (nested) sections
        {
            auto snode = root["strategy"];
            if (!snode || !snode.IsMap())
                snode = root["routing"];

            if (snode && snode.IsMap())
            {
                if (snode["default_strategy"])
                    cfg.strategy.default_strategy = parse_routing_strategy(
                        snode["default_strategy"].as<std::string>("best_price"));

                // VWAP -- check nested strategies.vwap or top-level
                auto vwap_node = snode["strategies"]["vwap"];
                if (vwap_node && vwap_node.IsMap())
                {
                    if (vwap_node["num_slices"])
                        cfg.strategy.vwap_num_slices = vwap_node["num_slices"].as<int32_t>(10);
                    if (vwap_node["interval_sec"])
                        cfg.strategy.vwap_duration_seconds = static_cast<int32_t>(
                            vwap_node["interval_sec"].as<double>(15.0) * cfg.strategy.vwap_num_slices);
                    if (vwap_node["participation_rate"])
                        cfg.strategy.vwap_urgency = vwap_node["participation_rate"].as<double>(0.05);
                }
                // Flat keys (backward compat)
                if (snode["vwap_num_slices"])
                    cfg.strategy.vwap_num_slices = snode["vwap_num_slices"].as<int32_t>(10);
                if (snode["vwap_duration_seconds"])
                    cfg.strategy.vwap_duration_seconds = snode["vwap_duration_seconds"].as<int32_t>(300);
                if (snode["vwap_urgency"])
                    cfg.strategy.vwap_urgency = snode["vwap_urgency"].as<double>(0.5);
                if (snode["sweep_min_slice"])
                    cfg.strategy.sweep_min_slice = snode["sweep_min_slice"].as<Quantity>(10);
                if (snode["ioc_slippage_ticks"])
                    cfg.strategy.ioc_slippage_ticks = snode["ioc_slippage_ticks"].as<int32_t>(2);
            }
        }

        // -- Risk --
        if (const auto &rnode = root["risk"]; rnode && rnode.IsMap())
        {
            if (rnode["max_order_quantity"])
                cfg.risk.max_order_quantity = rnode["max_order_quantity"].as<Quantity>(10000);
            if (rnode["max_order_notional"])
                cfg.risk.max_order_notional = to_price(rnode["max_order_notional"].as<double>(0.0));
            if (rnode["max_position_per_symbol"])
                cfg.risk.max_position_quantity = rnode["max_position_per_symbol"].as<Quantity>(0);
            if (rnode["max_position_notional"])
                cfg.risk.max_position_notional = to_price(rnode["max_position_notional"].as<double>(0.0));
            if (rnode["max_position_quantity"])
                cfg.risk.max_position_quantity = rnode["max_position_quantity"].as<Quantity>(0);
            // Nested rate_limiter section
            if (auto rl = rnode["rate_limiter"]; rl && rl.IsMap())
            {
                if (rl["max_orders_per_second"])
                    cfg.risk.max_orders_per_second = rl["max_orders_per_second"].as<int32_t>(100);
            }
            if (rnode["max_orders_per_second"])
                cfg.risk.max_orders_per_second = rnode["max_orders_per_second"].as<int32_t>(100);
            if (rnode["max_open_orders"])
                cfg.risk.max_open_orders = rnode["max_open_orders"].as<int32_t>(1000);
            // Kill switch max loss
            if (auto ks = rnode["kill_switch"]; ks && ks.IsMap())
            {
                if (ks["max_loss_threshold"])
                    cfg.risk.max_loss = to_price(ks["max_loss_threshold"].as<double>(0.0));
            }
            if (rnode["max_loss"])
                cfg.risk.max_loss = to_price(rnode["max_loss"].as<double>(0.0));
        }

        // -- Gateway --
        if (auto gnode = root["gateway"]; gnode && gnode.IsMap())
        {
            if (auto fix = gnode["fix"]; fix && fix.IsMap())
            {
                cfg.gateway.fix.enabled = fix["enabled"].as<bool>(false);
                cfg.gateway.fix.listen_port = fix["listen_port"].as<int32_t>(9876);
                cfg.gateway.fix.sender_comp_id = fix["sender_comp_id"].as<std::string>("SOR");
                cfg.gateway.fix.target_comp_id = fix["target_comp_id"].as<std::string>("CLIENT");
                cfg.gateway.fix.heartbeat_interval_sec = fix["heartbeat_interval_sec"].as<int32_t>(30);
            }
            if (auto api = gnode["api"]; api && api.IsMap())
            {
                cfg.gateway.api.enabled = api["enabled"].as<bool>(false);
                cfg.gateway.api.zmq_order_endpoint = api["zmq_endpoint"].as<std::string>("tcp://*:5555");
                cfg.gateway.api.zmq_market_data_endpoint = api["zmq_market_data_endpoint"].as<std::string>("tcp://*:5556");
                cfg.gateway.api.zmq_execution_endpoint = api["zmq_execution_endpoint"].as<std::string>("tcp://*:5557");
                cfg.gateway.api.max_message_size = api["max_message_size"].as<int32_t>(65536);
            }
        }

        // -- Metrics --
        if (auto mnode = root["metrics"]; mnode && mnode.IsMap())
        {
            if (auto prom = mnode["prometheus"]; prom && prom.IsMap())
            {
                cfg.metrics.enabled = prom["enabled"].as<bool>(true);
                cfg.metrics.bind_address = prom["endpoint"].as<std::string>("0.0.0.0");
                cfg.metrics.port = prom["port"].as<int32_t>(9090);
                cfg.metrics.path = prom["path"].as<std::string>("/metrics");
            }
        }
        // Backward compat: top-level enable_metrics / metrics_port
        if (root["enable_metrics"])
            cfg.enable_metrics = root["enable_metrics"].as<bool>(true);
        else
            cfg.enable_metrics = cfg.metrics.enabled;
        if (root["metrics_port"])
            cfg.metrics_port = root["metrics_port"].as<int32_t>(9090);
        else
            cfg.metrics_port = cfg.metrics.port;

        // -- Market Data --
        if (auto mdnode = root["market_data"]; mdnode && mdnode.IsMap())
        {
            cfg.market_data.provider = mdnode["provider"].as<std::string>("simulated");

            if (auto alpaca = mdnode["alpaca"]; alpaca && alpaca.IsMap())
            {
                cfg.market_data.alpaca_api_key = alpaca["api_key"].as<std::string>("");
                cfg.market_data.alpaca_api_secret = alpaca["api_secret"].as<std::string>("");
                cfg.market_data.alpaca_ws_url = alpaca["ws_url"].as<std::string>(
                    "wss://stream.data.alpaca.markets/v2/iex");
                cfg.market_data.alpaca_use_sip = alpaca["use_sip"].as<bool>(false);
            }

            if (mdnode["symbols"] && mdnode["symbols"].IsSequence())
            {
                for (const auto &snode : mdnode["symbols"])
                    cfg.market_data.symbols.push_back(snode.as<std::string>());
            }

            cfg.market_data.staleness_threshold_ms =
                mdnode["staleness_threshold_ms"].as<int32_t>(5000);
            cfg.market_data.reconnect_delay_sec =
                mdnode["reconnect_delay_sec"].as<int32_t>(5);
            cfg.market_data.max_reconnect_attempts =
                mdnode["max_reconnect_attempts"].as<int32_t>(10);

            // Expand ${ENV_VAR} patterns in API keys
            auto expand_env = [](std::string &val) {
                if (val.size() > 3 && val.front() == '$' && val[1] == '{' && val.back() == '}')
                {
                    std::string var_name = val.substr(2, val.size() - 3);
                    const char *env_val = std::getenv(var_name.c_str());
                    if (env_val)
                        val = env_val;
                    else
                        val.clear();
                }
            };
            expand_env(cfg.market_data.alpaca_api_key);
            expand_env(cfg.market_data.alpaca_api_secret);
        }

        config_ = std::move(cfg);
        return true;
    }

    // ---------------------------------------------------------------------------
    // Config validation
    // ---------------------------------------------------------------------------

    std::string validate_config(const SystemConfig &config)
    {
        // At least one venue must be enabled
        {
            bool has_enabled = false;
            for (const auto &v : config.venues)
            {
                if (v.enabled)
                {
                    has_enabled = true;

                    if (v.venue_id == 0)
                        return "Venue '" + v.name + "' has invalid id 0";
                    if (v.name.empty())
                        return "Venue id=" + std::to_string(v.venue_id) + " has empty name";
                    if (v.type != "simulated" && v.type != "fix")
                        return "Venue '" + v.name + "' has unknown adapter type '" + v.type + "' (expected simulated or fix)";
                    if (v.max_orders_per_second <= 0)
                        return "Venue '" + v.name + "' has invalid max_orders_per_second=" + std::to_string(v.max_orders_per_second);
                    if (v.fee_rate < 0.0 || v.fee_rate > 1.0)
                        return "Venue '" + v.name + "' has fee_rate outside [0, 1]: " + std::to_string(v.fee_rate);
                }
            }
            if (!has_enabled)
                return "No enabled venues configured";
        }

        // Duplicate venue IDs
        {
            std::unordered_map<VenueId, std::string> seen;
            for (const auto &v : config.venues)
            {
                if (!v.enabled)
                    continue;
                auto it = seen.find(v.venue_id);
                if (it != seen.end())
                    return "Duplicate venue id " + std::to_string(v.venue_id) + " ('" + it->second + "' and '" + v.name + "')";
                seen[v.venue_id] = v.name;
            }
        }

        // Risk sanity
        if (config.risk.max_order_quantity == 0)
            return "risk.max_order_quantity must be > 0";
        if (config.risk.max_orders_per_second <= 0)
            return "risk.max_orders_per_second must be > 0";

        // Log level
        {
            const auto &ll = config.log_level;
            if (ll != "trace" && ll != "debug" && ll != "info" &&
                ll != "warn" && ll != "error" && ll != "critical")
                return "Invalid log_level '" + ll + "'";
        }

        // Metrics port
        if (config.metrics.enabled || config.enable_metrics)
        {
            int port = config.metrics.port;
            if (port <= 0 || port > 65535)
                return "Invalid metrics port " + std::to_string(port);
        }

        // ZMQ endpoints: basic format check (must start with tcp://, ipc://, or inproc://)
        if (config.gateway.api.enabled)
        {
            auto valid_endpoint = [](const std::string &ep) {
                return ep.starts_with("tcp://") ||
                       ep.starts_with("ipc://") ||
                       ep.starts_with("inproc://");
            };
            if (!valid_endpoint(config.gateway.api.zmq_order_endpoint))
                return "Invalid ZMQ order endpoint: " + config.gateway.api.zmq_order_endpoint;
            if (!valid_endpoint(config.gateway.api.zmq_market_data_endpoint))
                return "Invalid ZMQ market data endpoint: " + config.gateway.api.zmq_market_data_endpoint;
            if (!valid_endpoint(config.gateway.api.zmq_execution_endpoint))
                return "Invalid ZMQ execution endpoint: " + config.gateway.api.zmq_execution_endpoint;
        }

        // Strategy params
        if (config.strategy.vwap_num_slices <= 0)
            return "strategy.vwap_num_slices must be > 0";

        // Market data provider
        {
            const auto &md = config.market_data;
            if (md.provider != "simulated" && md.provider != "alpaca" && md.provider != "replay")
                return "Unknown market_data.provider '" + md.provider + "' (expected simulated, alpaca, or replay)";
            if (md.provider == "alpaca")
            {
                if (md.alpaca_api_key.empty())
                    return "market_data.alpaca.api_key is required when provider is 'alpaca'";
                if (md.alpaca_api_secret.empty())
                    return "market_data.alpaca.api_secret is required when provider is 'alpaca'";
                if (md.symbols.empty())
                    return "market_data.symbols must not be empty when provider is 'alpaca'";
            }
            if (md.staleness_threshold_ms <= 0)
                return "market_data.staleness_threshold_ms must be > 0";
            if (md.reconnect_delay_sec <= 0)
                return "market_data.reconnect_delay_sec must be > 0";
        }

        return {}; // empty = valid
    }

} // namespace sor::infra
