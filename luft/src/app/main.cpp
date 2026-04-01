// ──────────────────────────────────────────────
// luft — high-performance flight simulator
// Application entry point
// ──────────────────────────────────────────────

#include "../core/simulation_engine.h"
#include "../core/config.h"
#include "../core/logger.h"
#include "../core/net/network_service.h"
#include "../core/net/protocol.h"

#if LUFT_HAS_UI
#include "../ui/ui_app.h"
#endif

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <thread>

// ──────────────────────────────────────────────
// Signal handling
// ──────────────────────────────────────────────

static std::atomic<bool> g_quit_flag{false};

static void signal_handler(int sig)
{
    (void)sig;
    g_quit_flag.store(true, std::memory_order_release);
}

static void install_signal_handlers()
{
    struct sigaction sa{};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART — allow interrupted syscalls to return

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

// ──────────────────────────────────────────────
// Command-line argument parsing
// ──────────────────────────────────────────────

struct AppOptions
{
    std::string config_path;
    bool headless = false;
    bool no_ui = false;
    bool show_help = false;
};

static void print_usage(const char *argv0)
{
    std::fprintf(stderr,
                 "Usage: %s [options]\n"
                 "\n"
                 "Options:\n"
                 "  --config <path>   Path to configuration file (.cfg)\n"
                 "  --headless        Run without UI (simulation + network only)\n"
                 "  --no-ui           Alias for --headless\n"
                 "  --help            Show this help message\n"
                 "\n",
                 argv0);
}

static AppOptions parse_args(int argc, char *argv[])
{
    AppOptions opts;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--config") == 0)
        {
            if (i + 1 < argc)
            {
                opts.config_path = argv[++i];
            }
            else
            {
                std::fprintf(stderr, "Error: --config requires a path argument\n");
                std::exit(1);
            }
        }
        else if (std::strcmp(argv[i], "--headless") == 0)
        {
            opts.headless = true;
        }
        else if (std::strcmp(argv[i], "--no-ui") == 0)
        {
            opts.no_ui = true;
        }
        else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0)
        {
            opts.show_help = true;
        }
        else
        {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return opts;
}

// ──────────────────────────────────────────────
// Logger initialization from config
// ──────────────────────────────────────────────

static void init_logger(const luft::Config &config)
{
    auto &logger = luft::Logger::instance();

    // Parse log level string
    luft::LogLevel level = luft::LogLevel::Info;
    if (config.log_level == "trace")
        level = luft::LogLevel::Trace;
    else if (config.log_level == "debug")
        level = luft::LogLevel::Debug;
    else if (config.log_level == "info")
        level = luft::LogLevel::Info;
    else if (config.log_level == "warn")
        level = luft::LogLevel::Warn;
    else if (config.log_level == "error")
        level = luft::LogLevel::Error;

    logger.set_level(level);
    logger.set_console_enabled(config.log_console);

    if (!config.log_file.empty())
    {
        logger.set_file(config.log_file);
    }
}

// ──────────────────────────────────────────────
// Telemetry console output (headless mode)
// ──────────────────────────────────────────────

static void print_state_summary(const luft::AircraftState &state, double sim_time)
{
    luft::Vec3 euler = state.orientation.to_euler();

    std::fprintf(stdout,
                 "\r[t=%7.2f] alt=%6.0fm  V=%5.1fm/s  hdg=%5.1f  pitch=%5.1f  roll=%5.1f  fuel=%5.1fkg  ",
                 sim_time,
                 state.altitude_msl,
                 state.airspeed,
                 euler.z * luft::kRadToDeg, // yaw -> heading
                 euler.y * luft::kRadToDeg, // pitch
                 euler.x * luft::kRadToDeg, // roll
                 state.fuel_mass);

    std::fflush(stdout);
}

// ──────────────────────────────────────────────
// Network command dispatch
// ──────────────────────────────────────────────

static void dispatch_sim_command(luft::SimulationEngine &engine,
                                 const luft::Config &config,
                                 luft::SimCommandType cmd)
{
    switch (cmd)
    {
    case luft::SimCommandType::Start:
        engine.start();
        break;
    case luft::SimCommandType::Pause:
        engine.pause();
        break;
    case luft::SimCommandType::Resume:
        engine.resume();
        break;
    case luft::SimCommandType::Reset:
        engine.reset(config);
        engine.start();
        break;
    case luft::SimCommandType::Stop:
        engine.stop();
        break;
    }
}

// ──────────────────────────────────────────────
// Main
// ──────────────────────────────────────────────

int main(int argc, char *argv[])
{
    // ── 1. Parse command-line arguments ──
    AppOptions opts = parse_args(argc, argv);

    if (opts.show_help)
    {
        print_usage(argv[0]);
        return 0;
    }

    bool ui_disabled = opts.headless || opts.no_ui;

    // ── 2. Load configuration ──
    luft::Config config;
    if (!opts.config_path.empty())
    {
        config = luft::load_config(opts.config_path);
    }
    else
    {
        config = luft::default_config();
    }

    if (ui_disabled)
    {
        config.ui_enabled = false;
    }

    // ── 3. Validate configuration ──
    std::string config_error;
    if (!luft::validate_config(config, config_error))
    {
        std::fprintf(stderr, "Configuration error: %s\n", config_error.c_str());
        return 1;
    }

    // ── 4. Initialize logger ──
    init_logger(config);

    // ── 5. Startup banner ──
    LOG_INFO("════════════════════════════════════════════");
    LOG_INFO("  luft flight simulator");
    LOG_INFO("  mode: %s", config.ui_enabled ? "windowed" : "headless");
    LOG_INFO("  sim rate: %.0f Hz (dt=%.4fs)", 1.0 / config.time_step, config.time_step);
    LOG_INFO("  telemetry rate: %.0f Hz", config.telemetry_rate_hz);
    LOG_INFO("════════════════════════════════════════════");

    // ── 6. Initialize simulation engine ──
    luft::SimulationEngine sim_engine;
    sim_engine.initialize(config);

    // ── 7. Initialize network service ──
    std::unique_ptr<luft::NetworkService> network_service;
    std::thread network_thread;

    if (config.networking_enabled)
    {
        network_service = std::make_unique<luft::NetworkService>();

        network_service->set_command_callback(
            [&sim_engine, &config](luft::SimCommandType cmd)
            {
                dispatch_sim_command(sim_engine, config, cmd);
            });

        network_service->set_control_callback(
            [&sim_engine](const luft::ControlInput &input)
            {
                sim_engine.set_control_input(input);
            });

        if (!network_service->start(config.telemetry_host, config.telemetry_port,
                                    config.command_host, config.command_port))
        {
            LOG_ERROR("Failed to start network service — continuing without networking");
            network_service.reset();
        }
        else
        {
            LOG_INFO("Network service started: telemetry=%s:%u, command=%s:%u",
                     config.telemetry_host.c_str(), config.telemetry_port,
                     config.command_host.c_str(), config.command_port);

            // Launch network poll loop on a dedicated thread.
            // The thread exits when g_quit_flag is set; poll() returns
            // quickly thanks to the short epoll timeout.
            network_thread = std::thread([&network_service]()
                                         {
                while (!g_quit_flag.load(std::memory_order_acquire)) {
                    network_service->poll(10);  // 10ms epoll timeout
                } });
        }
    }

    // ── 8. Initialize UI (optional) ──
#if LUFT_HAS_UI
    std::unique_ptr<luft::UiApp> ui;
    if (config.ui_enabled)
    {
        ui = std::make_unique<luft::UiApp>();
        if (!ui->initialize(config.window_width, config.window_height, "luft flight simulator"))
        {
            LOG_ERROR("Failed to initialize UI — falling back to headless");
            ui.reset();
            config.ui_enabled = false;
        }
    }
#else
    if (config.ui_enabled)
    {
        LOG_WARN("Built without UI support — running headless");
        config.ui_enabled = false;
    }
#endif

    // ── 9. Install signal handlers and start simulation ──
    install_signal_handlers();
    sim_engine.start();

    // ── 10. Main loop ──
    using Clock = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double>;

    auto sim_start = Clock::now();
    double next_telemetry = 0.0;
    double telemetry_interval = (config.telemetry_rate_hz > 0.0)
                                    ? (1.0 / config.telemetry_rate_hz)
                                    : 0.05; // default 20 Hz

    LOG_INFO("Entering main loop");

    while (!g_quit_flag.load(std::memory_order_acquire))
    {
        auto now = Clock::now();
        double elapsed = Duration(now - sim_start).count();

        // ── Step simulation to catch up with wall-clock time ──
        constexpr int kMaxStepsPerFrame = 20;
        int steps_this_frame = 0;

        while (sim_engine.get_sim_time() < elapsed &&
               sim_engine.get_sim_state() == luft::SimState::Running &&
               steps_this_frame < kMaxStepsPerFrame)
        {
            sim_engine.step();
            ++steps_this_frame;
        }

        // ── Check for simulation error ──
        if (sim_engine.get_sim_state() == luft::SimState::Error)
        {
            LOG_ERROR("Simulation entered error state — shutting down");
            break;
        }

        // ── Check max sim time ──
        if (config.max_sim_time > 0.0 && sim_engine.get_sim_time() >= config.max_sim_time)
        {
            LOG_INFO("Reached max simulation time (%.1fs) — shutting down", config.max_sim_time);
            break;
        }

        // ── Publish telemetry at configured rate ──
        double current_sim_time = sim_engine.get_sim_time();
        if (current_sim_time >= next_telemetry)
        {
            if (network_service)
            {
                network_service->publish_telemetry(sim_engine.get_state(),
                                                   sim_engine.get_sim_state());
            }
            next_telemetry += telemetry_interval;

            if (!config.ui_enabled)
            {
                print_state_summary(sim_engine.get_state(), current_sim_time);
            }
        }

        // ── UI frame ──
#if LUFT_HAS_UI
        if (ui)
        {
            if (!ui->process_events())
            {
                break; // window closed
            }

            // Collect keyboard flight input
            luft::ControlInput kb_input;
            ui->get_keyboard_control_input(kb_input);
            sim_engine.set_control_input(kb_input);

            // Render
            ui->begin_frame();

            auto state = sim_engine.get_state();
            ui->render_telemetry(state, sim_engine.get_sim_state(), current_sim_time);

            luft::ControlInput ui_input = kb_input;
            ui->render_controls(ui_input);
            sim_engine.set_control_input(ui_input);

            bool start_clicked = false, pause_clicked = false, resume_clicked = false;
            bool reset_clicked = false, stop_clicked = false;
            ui->render_sim_controls(sim_engine.get_sim_state(),
                                    &start_clicked, &pause_clicked, &resume_clicked,
                                    &reset_clicked, &stop_clicked);

            if (start_clicked)
                sim_engine.start();
            if (pause_clicked)
                sim_engine.pause();
            if (resume_clicked)
                sim_engine.resume();
            if (reset_clicked)
            {
                sim_engine.reset(config);
                sim_engine.start();
            }
            if (stop_clicked)
                sim_engine.stop();

            ui->render_connection_status(0, 0); // TODO: expose client count from network service
            ui->render_log_panel();
            ui->end_frame();
        }
#endif

        // ── Don't burn CPU ──
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // ── 11. Shutdown sequence ──
    LOG_INFO("Shutting down...");

    sim_engine.stop();

    if (network_service)
    {
        network_service->stop();
    }

    if (network_thread.joinable())
    {
        network_thread.join();
    }

#if LUFT_HAS_UI
    if (ui)
    {
        ui->shutdown();
    }
#endif

    // Final newline after \r-based console output
    if (!config.ui_enabled)
    {
        std::fprintf(stdout, "\n");
    }

    LOG_INFO("Simulation ended: t=%.3fs, ticks=%lu", sim_engine.get_sim_time(),
             sim_engine.get_tick_count());
    LOG_INFO("luft shutdown complete");

    luft::Logger::instance().flush();

    return 0;
}
