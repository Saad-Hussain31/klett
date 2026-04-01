#pragma once

#include <cstdint>
#include <string>

namespace luft
{

    // ──────────────────────────────────────────────
    // Config — all runtime configuration
    // Loaded from a .cfg file with "key = value" lines
    // ──────────────────────────────────────────────

    struct Config
    {
        // Simulation
        double time_step = 0.01;   // seconds (100 Hz)
        double max_sim_time = 0.0; // 0 = unlimited

        // Network
        std::string telemetry_host = "0.0.0.0";
        uint16_t telemetry_port = 5000;
        std::string command_host = "0.0.0.0";
        uint16_t command_port = 5001;
        bool networking_enabled = true;
        double telemetry_rate_hz = 20.0;

        // Logging
        std::string log_level = "info";
        std::string log_file = "";
        bool log_console = true;

        // UI
        bool ui_enabled = true;
        int window_width = 1280;
        int window_height = 720;

        // Aircraft
        std::string aircraft_type = "cessna172";
        double initial_altitude_m = 1000.0;
        double initial_airspeed_ms = 50.0;
        double initial_heading_deg = 0.0;
        double initial_fuel_kg = 100.0;

        // Environment
        double wind_north_ms = 0.0;
        double wind_east_ms = 0.0;
        double wind_down_ms = 0.0;
        double gust_intensity = 0.0;

        // Control
        double elevator_sensitivity = 1.0;
        double aileron_sensitivity = 1.0;
        double rudder_sensitivity = 1.0;
    };

    // Load config from a .cfg file (key = value format, # for comments)
    Config load_config(const std::string &path);

    // Validate all config values; returns false with error message if invalid
    bool validate_config(const Config &cfg, std::string &error);

    // Returns a Config with all default values
    Config default_config();

} // namespace luft
