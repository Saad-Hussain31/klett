#include <gtest/gtest.h>
#include "config.h"
#include <fstream>
#include <cstdio>
#include <string>
#include <filesystem>

using namespace luft;

class ConfigTest : public ::testing::Test
{
protected:
    std::string tmp_path;

    void SetUp() override
    {
        tmp_path = std::filesystem::temp_directory_path() / "luft_test_config.cfg";
    }

    void TearDown() override
    {
        std::remove(tmp_path.c_str());
    }

    void write_config(const std::string &content)
    {
        std::ofstream f(tmp_path);
        f << content;
        f.close();
    }
};

TEST_F(ConfigTest, DefaultConfigIsValid)
{
    Config cfg = default_config();
    std::string error;
    EXPECT_TRUE(validate_config(cfg, error)) << error;
}

TEST_F(ConfigTest, DefaultConfigValues)
{
    Config cfg = default_config();
    EXPECT_DOUBLE_EQ(cfg.time_step, 0.01);
    EXPECT_EQ(cfg.telemetry_port, 5000);
    EXPECT_EQ(cfg.command_port, 5001);
    EXPECT_DOUBLE_EQ(cfg.initial_altitude_m, 1000.0);
    EXPECT_DOUBLE_EQ(cfg.initial_airspeed_ms, 50.0);
}

TEST_F(ConfigTest, ValidConfigFileParses)
{
    write_config(
        "time_step = 0.02\n"
        "telemetry_port = 6000\n"
        "command_port = 6001\n"
        "initial_altitude_m = 2000.0\n"
        "initial_airspeed_ms = 60.0\n"
        "wind_north_ms = 5.0\n"
        "gust_intensity = 0.3\n");

    Config cfg = load_config(tmp_path);
    EXPECT_DOUBLE_EQ(cfg.time_step, 0.02);
    EXPECT_EQ(cfg.telemetry_port, 6000);
    EXPECT_EQ(cfg.command_port, 6001);
    EXPECT_DOUBLE_EQ(cfg.initial_altitude_m, 2000.0);
    EXPECT_DOUBLE_EQ(cfg.initial_airspeed_ms, 60.0);
    EXPECT_DOUBLE_EQ(cfg.wind_north_ms, 5.0);
    EXPECT_DOUBLE_EQ(cfg.gust_intensity, 0.3);
}

TEST_F(ConfigTest, InvalidTimeStepZero)
{
    Config cfg = default_config();
    cfg.time_step = 0.0;
    std::string error;
    EXPECT_FALSE(validate_config(cfg, error));
}

TEST_F(ConfigTest, InvalidTimeStepNegative)
{
    Config cfg = default_config();
    cfg.time_step = -0.01;
    std::string error;
    EXPECT_FALSE(validate_config(cfg, error));
}

TEST_F(ConfigTest, InvalidTimeStepTooLarge)
{
    Config cfg = default_config();
    cfg.time_step = 0.2;
    std::string error;
    EXPECT_FALSE(validate_config(cfg, error));
}

TEST_F(ConfigTest, InvalidPortZero)
{
    Config cfg = default_config();
    cfg.telemetry_port = 0;
    std::string error;
    EXPECT_FALSE(validate_config(cfg, error));
}

TEST_F(ConfigTest, InvalidDuplicatePorts)
{
    Config cfg = default_config();
    cfg.telemetry_port = 5000;
    cfg.command_port = 5000;
    std::string error;
    EXPECT_FALSE(validate_config(cfg, error));
}

TEST_F(ConfigTest, MissingFileReturnsDefaults)
{
    Config cfg = load_config("/nonexistent/path/config.cfg");
    Config def = default_config();
    EXPECT_DOUBLE_EQ(cfg.time_step, def.time_step);
    EXPECT_EQ(cfg.telemetry_port, def.telemetry_port);
}

TEST_F(ConfigTest, CommentsAreIgnored)
{
    write_config(
        "# This is a comment\n"
        "time_step = 0.05\n"
        "# Another comment\n"
        "initial_altitude_m = 3000.0\n");
    Config cfg = load_config(tmp_path);
    EXPECT_DOUBLE_EQ(cfg.time_step, 0.05);
    EXPECT_DOUBLE_EQ(cfg.initial_altitude_m, 3000.0);
}

TEST_F(ConfigTest, QuotedStringValues)
{
    write_config(
        "telemetry_host = \"127.0.0.1\"\n"
        "log_level = \"debug\"\n");
    Config cfg = load_config(tmp_path);
    EXPECT_EQ(cfg.telemetry_host, "127.0.0.1");
    EXPECT_EQ(cfg.log_level, "debug");
}

TEST_F(ConfigTest, EmptyLinesSkipped)
{
    write_config(
        "\n"
        "time_step = 0.03\n"
        "\n"
        "\n"
        "initial_fuel_kg = 50.0\n");
    Config cfg = load_config(tmp_path);
    EXPECT_DOUBLE_EQ(cfg.time_step, 0.03);
    EXPECT_DOUBLE_EQ(cfg.initial_fuel_kg, 50.0);
}

TEST_F(ConfigTest, UnknownKeysSkipped)
{
    write_config(
        "time_step = 0.04\n"
        "unknown_key = 42\n"
        "initial_fuel_kg = 80.0\n");
    Config cfg = load_config(tmp_path);
    EXPECT_DOUBLE_EQ(cfg.time_step, 0.04);
    EXPECT_DOUBLE_EQ(cfg.initial_fuel_kg, 80.0);
}

TEST_F(ConfigTest, WhitespaceHandling)
{
    write_config(
        "  time_step   =   0.04  \n"
        "  initial_altitude_m=500.0\n");
    Config cfg = load_config(tmp_path);
    EXPECT_DOUBLE_EQ(cfg.time_step, 0.04);
    EXPECT_DOUBLE_EQ(cfg.initial_altitude_m, 500.0);
}

TEST_F(ConfigTest, BooleanValues)
{
    write_config(
        "networking_enabled = false\n"
        "ui_enabled = true\n"
        "log_console = yes\n");
    Config cfg = load_config(tmp_path);
    EXPECT_FALSE(cfg.networking_enabled);
    EXPECT_TRUE(cfg.ui_enabled);
    EXPECT_TRUE(cfg.log_console);
}

TEST_F(ConfigTest, InvalidGustIntensity)
{
    Config cfg = default_config();
    cfg.gust_intensity = 1.5;
    std::string error;
    EXPECT_FALSE(validate_config(cfg, error));
}

TEST_F(ConfigTest, InvalidSensitivity)
{
    Config cfg = default_config();
    cfg.elevator_sensitivity = 0.0;
    std::string error;
    EXPECT_FALSE(validate_config(cfg, error));
}
