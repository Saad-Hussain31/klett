#include "config.h"
#include "logger.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace luft
{

    // ──────────────────────────────────────────────
    // Helpers
    // ──────────────────────────────────────────────

    static std::string trim(const std::string &s)
    {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            return "";
        auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    static std::string to_lower(const std::string &s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c)
                       { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    static bool parse_bool(const std::string &val)
    {
        std::string v = to_lower(trim(val));
        return (v == "true" || v == "1" || v == "yes");
    }

    // ──────────────────────────────────────────────
    // load_config
    // ──────────────────────────────────────────────

    Config load_config(const std::string &path)
    {
        Config cfg;

        std::ifstream file(path);
        if (!file.is_open())
        {
            LOG_WARN("Config file not found: %s — using defaults", path.c_str());
            return cfg;
        }

        LOG_INFO("Loading config from %s", path.c_str());

        std::string line;
        int line_num = 0;
        while (std::getline(file, line))
        {
            ++line_num;
            line = trim(line);

            // Skip empty lines and comments
            if (line.empty() || line[0] == '#')
                continue;

            auto eq_pos = line.find('=');
            if (eq_pos == std::string::npos)
            {
                LOG_WARN("Config line %d: missing '=', skipping: %s", line_num, line.c_str());
                continue;
            }

            std::string key = to_lower(trim(line.substr(0, eq_pos)));
            std::string val = trim(line.substr(eq_pos + 1));

            // Strip surrounding quotes from string values
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);

            // Map key to config field
            try
            {
                // Simulation
                if (key == "time_step")
                    cfg.time_step = std::stod(val);
                else if (key == "max_sim_time")
                    cfg.max_sim_time = std::stod(val);
                // Network
                else if (key == "telemetry_host")
                    cfg.telemetry_host = val;
                else if (key == "telemetry_port")
                    cfg.telemetry_port = static_cast<uint16_t>(std::stoi(val));
                else if (key == "command_host")
                    cfg.command_host = val;
                else if (key == "command_port")
                    cfg.command_port = static_cast<uint16_t>(std::stoi(val));
                else if (key == "networking_enabled")
                    cfg.networking_enabled = parse_bool(val);
                else if (key == "telemetry_rate_hz")
                    cfg.telemetry_rate_hz = std::stod(val);
                // Logging
                else if (key == "log_level")
                    cfg.log_level = to_lower(val);
                else if (key == "log_file")
                    cfg.log_file = val;
                else if (key == "log_console")
                    cfg.log_console = parse_bool(val);
                // UI
                else if (key == "ui_enabled")
                    cfg.ui_enabled = parse_bool(val);
                else if (key == "window_width")
                    cfg.window_width = std::stoi(val);
                else if (key == "window_height")
                    cfg.window_height = std::stoi(val);
                // Aircraft
                else if (key == "aircraft_type")
                    cfg.aircraft_type = val;
                else if (key == "initial_altitude_m")
                    cfg.initial_altitude_m = std::stod(val);
                else if (key == "initial_airspeed_ms")
                    cfg.initial_airspeed_ms = std::stod(val);
                else if (key == "initial_heading_deg")
                    cfg.initial_heading_deg = std::stod(val);
                else if (key == "initial_fuel_kg")
                    cfg.initial_fuel_kg = std::stod(val);
                // Environment
                else if (key == "wind_north_ms")
                    cfg.wind_north_ms = std::stod(val);
                else if (key == "wind_east_ms")
                    cfg.wind_east_ms = std::stod(val);
                else if (key == "wind_down_ms")
                    cfg.wind_down_ms = std::stod(val);
                else if (key == "gust_intensity")
                    cfg.gust_intensity = std::stod(val);
                // Control
                else if (key == "elevator_sensitivity")
                    cfg.elevator_sensitivity = std::stod(val);
                else if (key == "aileron_sensitivity")
                    cfg.aileron_sensitivity = std::stod(val);
                else if (key == "rudder_sensitivity")
                    cfg.rudder_sensitivity = std::stod(val);
                else
                {
                    LOG_WARN("Config line %d: unknown key '%s'", line_num, key.c_str());
                }
            }
            catch (const std::exception &e)
            {
                LOG_WARN("Config line %d: parse error for '%s': %s", line_num, key.c_str(), e.what());
            }
        }

        return cfg;
    }

    // ──────────────────────────────────────────────
    // validate_config
    // ──────────────────────────────────────────────

    bool validate_config(const Config &cfg, std::string &error)
    {
        if (cfg.time_step <= 0.0 || cfg.time_step > 0.1)
        {
            error = "time_step must be in (0, 0.1]";
            return false;
        }

        if (cfg.max_sim_time < 0.0)
        {
            error = "max_sim_time must be >= 0";
            return false;
        }

        if (cfg.telemetry_port == 0)
        {
            error = "telemetry_port must be > 0";
            return false;
        }

        if (cfg.command_port == 0)
        {
            error = "command_port must be > 0";
            return false;
        }

        if (cfg.telemetry_port == cfg.command_port)
        {
            error = "telemetry_port and command_port must be different";
            return false;
        }

        if (cfg.telemetry_rate_hz <= 0.0)
        {
            error = "telemetry_rate_hz must be > 0";
            return false;
        }

        if (cfg.window_width < 320 || cfg.window_height < 240)
        {
            error = "window dimensions too small (min 320x240)";
            return false;
        }

        if (cfg.initial_altitude_m < 0.0 || cfg.initial_altitude_m > 50000.0)
        {
            error = "initial_altitude_m must be in [0, 50000]";
            return false;
        }

        if (cfg.initial_airspeed_ms < 0.0 || cfg.initial_airspeed_ms > 500.0)
        {
            error = "initial_airspeed_ms must be in [0, 500]";
            return false;
        }

        if (cfg.initial_heading_deg < 0.0 || cfg.initial_heading_deg >= 360.0)
        {
            error = "initial_heading_deg must be in [0, 360)";
            return false;
        }

        if (cfg.initial_fuel_kg < 0.0)
        {
            error = "initial_fuel_kg must be >= 0";
            return false;
        }

        if (cfg.gust_intensity < 0.0 || cfg.gust_intensity > 1.0)
        {
            error = "gust_intensity must be in [0, 1]";
            return false;
        }

        if (cfg.elevator_sensitivity <= 0.0 || cfg.elevator_sensitivity > 5.0)
        {
            error = "elevator_sensitivity must be in (0, 5]";
            return false;
        }

        if (cfg.aileron_sensitivity <= 0.0 || cfg.aileron_sensitivity > 5.0)
        {
            error = "aileron_sensitivity must be in (0, 5]";
            return false;
        }

        if (cfg.rudder_sensitivity <= 0.0 || cfg.rudder_sensitivity > 5.0)
        {
            error = "rudder_sensitivity must be in (0, 5]";
            return false;
        }

        return true;
    }

    // ──────────────────────────────────────────────
    // default_config
    // ──────────────────────────────────────────────

    Config default_config()
    {
        return Config{};
    }

} // namespace luft
