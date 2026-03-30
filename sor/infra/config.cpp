#include "infra/config.h"
#include "infra/logging.h"

#include <yaml-cpp/yaml.h>

#include <filesystem>
#include <fstream>
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

        // System-level fields
        if (root["log_level"])
            cfg.log_level = root["log_level"].as<std::string>("info");
        if (root["log_file"])
            cfg.log_file = root["log_file"].as<std::string>("");
        if (root["enable_metrics"])
            cfg.enable_metrics = root["enable_metrics"].as<bool>(true);
        if (root["metrics_port"])
            cfg.metrics_port = root["metrics_port"].as<int32_t>(9090);
        if (root["data_dir"])
            cfg.data_dir = root["data_dir"].as<std::string>("");

        // -- Venues --
        if (root["venues"] && root["venues"].IsSequence())
        {
            for (const auto &vnode : root["venues"])
            {
                VenueConfig vc;
                vc.venue_id = vnode["venue_id"].as<VenueId>(0);
                vc.name = vnode["name"].as<std::string>("");
                vc.type = vnode["type"].as<std::string>("simulated");
                vc.endpoint = vnode["endpoint"].as<std::string>("");
                vc.fee_rate = vnode["fee_rate"].as<double>(0.001);
                vc.enabled = vnode["enabled"].as<bool>(true);
                vc.max_orders_per_second = vnode["max_orders_per_second"].as<int32_t>(100);
                cfg.venues.push_back(std::move(vc));
            }
        }

        // -- Strategy --
        if (const auto &snode = root["strategy"]; snode && snode.IsMap())
        {
            if (snode["default_strategy"])
                cfg.strategy.default_strategy = parse_routing_strategy(
                    snode["default_strategy"].as<std::string>("best_price"));
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

        // -- Risk --
        if (const auto &rnode = root["risk"]; rnode && rnode.IsMap())
        {
            if (rnode["max_order_quantity"])
                cfg.risk.max_order_quantity = rnode["max_order_quantity"].as<Quantity>(10000);
            if (rnode["max_order_notional"])
                cfg.risk.max_order_notional = to_price(rnode["max_order_notional"].as<double>(0.0));
            if (rnode["max_position_notional"])
                cfg.risk.max_position_notional = to_price(rnode["max_position_notional"].as<double>(0.0));
            if (rnode["max_position_quantity"])
                cfg.risk.max_position_quantity = rnode["max_position_quantity"].as<Quantity>(0);
            if (rnode["max_orders_per_second"])
                cfg.risk.max_orders_per_second = rnode["max_orders_per_second"].as<int32_t>(100);
            if (rnode["max_open_orders"])
                cfg.risk.max_open_orders = rnode["max_open_orders"].as<int32_t>(1000);
            if (rnode["max_loss"])
                cfg.risk.max_loss = to_price(rnode["max_loss"].as<double>(0.0));
        }

        config_ = std::move(cfg);
        return true;
    }

} // namespace sor::infra
