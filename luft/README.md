# luft

A deterministic, fixed-step flight dynamics simulator written in modern C++20. Luft models 6-DOF rigid-body aircraft dynamics with stability-derivative aerodynamics, an ISA atmosphere, a first-order engine model, and optional wind/gust effects. It exposes aircraft state over TCP for external tooling and optionally renders a real-time UI via SDL2 and Dear ImGui.

## Features

- **6-DOF flight dynamics** with RK4 integration and quaternion orientation
- **Stability-derivative aerodynamics** (longitudinal + lateral-directional)
- **ISA atmosphere model** (troposphere + stratosphere)
- **First-order engine model** with fuel consumption tracking
- **Wind and gust model** (constant wind + stochastic gusts)
- **TCP networking** -- dual-port architecture for telemetry broadcast and command ingestion, driven by epoll
- **Optional SDL2/ImGui UI** -- live telemetry display, control sliders, simulation controls, and log panel
- **Deterministic fixed-step simulation** -- reproducible runs suitable for testing and CI
- **Configurable** via `.cfg` files using a simple `key = value` format

## Requirements

| Dependency | Minimum version | Notes |
|---|---|---|
| Linux | kernel 5.x+ | Required for epoll networking |
| GCC | 11+ | C++20 support required |
| CMake | 3.20+ | Build system |
| pthreads | (system) | Threading support |
| SDL2 | 2.30+ | Fetched automatically when UI is enabled |
| OpenGL | 3.x+ | Only needed when UI is enabled |
| lcov / genhtml | any | Only needed for coverage reports |

## Building

All builds use an out-of-source `build/` directory.

### Headless (no UI dependencies)

```bash
cmake -S . -B build -DLUFT_BUILD_UI=OFF
cmake --build build -j$(nproc)
```

### With UI (SDL2 + ImGui, fetched via FetchContent)

```bash
cmake -S . -B build -DLUFT_BUILD_UI=ON
cmake --build build -j$(nproc)
```

### With tests

Tests are enabled by default. To be explicit:

```bash
cmake -S . -B build -DLUFT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

### With coverage

```bash
cmake -S . -B build-coverage \
    -DCMAKE_BUILD_TYPE=Debug \
    -DLUFT_COVERAGE=ON \
    -DLUFT_BUILD_UI=OFF \
    -DLUFT_BUILD_TESTS=ON
cmake --build build-coverage -j$(nproc)
```

Or use the helper script (see [Coverage](#coverage) below).

### Combining options

All CMake options can be combined freely:

```bash
cmake -S . -B build -DLUFT_BUILD_UI=ON -DLUFT_BUILD_TESTS=ON
cmake --build build -j$(nproc)
```

## Running

```bash
# Headless with a configuration file
./build/luft_sim --headless --config config/default.cfg

# Headless with built-in defaults (no config file needed)
./build/luft_sim --headless

# With UI (requires a build with -DLUFT_BUILD_UI=ON)
./build/luft_sim --config config/default.cfg
```

### Command-line flags

| Flag | Description |
|---|---|
| `--config <path>` | Path to a `.cfg` configuration file |
| `--headless` | Run without UI (simulation + network only) |
| `--no-ui` | Alias for `--headless` |
| `--help`, `-h` | Print usage information and exit |

### Configuration

Configuration uses a simple `key = value` format. Lines starting with `#` are comments. See `config/default.cfg` for all available keys. Key categories:

| Category | Keys |
|---|---|
| Simulation | `time_step`, `max_sim_time` |
| Network | `networking_enabled`, `telemetry_host`, `telemetry_port`, `command_host`, `command_port`, `telemetry_rate_hz` |
| Logging | `log_level` (trace/debug/info/warn/error), `log_file`, `log_console` |
| UI | `ui_enabled`, `window_width`, `window_height` |
| Aircraft | `aircraft_type`, `initial_altitude_m`, `initial_airspeed_ms`, `initial_heading_deg`, `initial_fuel_kg` |
| Environment | `wind_north_ms`, `wind_east_ms`, `wind_down_ms`, `gust_intensity` |
| Controls | `elevator_sensitivity`, `aileron_sensitivity`, `rudder_sensitivity` |

## Project structure

```
luft/
├── CMakeLists.txt
├── config/
│   └── default.cfg              # Default configuration
├── docs/
│   ├── architecture.md          # System architecture and design
│   └── flight_dynamics_theory.md # Flight dynamics and aero theory
├── scripts/
│   └── coverage.sh              # Code coverage report generator
├── src/
│   ├── app/
│   │   └── main.cpp             # Entry point, main loop
│   ├── core/
│   │   ├── aerodynamics.h/cpp   # Stability-derivative aero model
│   │   ├── aircraft_state.h     # AircraftState, AircraftParams, ControlInput, SimState
│   │   ├── atmosphere.h/cpp     # ISA atmosphere model
│   │   ├── config.h/cpp         # Configuration loading and validation
│   │   ├── engine_model.h/cpp   # First-order thrust model with fuel burn
│   │   ├── flight_dynamics.h/cpp # RK4 integrator, 6-DOF equations of motion
│   │   ├── input_manager.h/cpp  # Key mapping and input processing
│   │   ├── logger.h/cpp         # Thread-safe singleton logger
│   │   ├── math_types.h         # Vec3, Quaternion, constants
│   │   ├── simulation_engine.h/cpp # Top-level simulation orchestrator
│   │   ├── wind_model.h/cpp     # Constant + gust wind model
│   │   └── net/
│   │       ├── message_codec.h/cpp  # Wire framing and serialization
│   │       ├── network_service.h/cpp # Epoll event loop, client management
│   │       ├── protocol.h          # MessageHeader, MessageType enums
│   │       └── socket.h/cpp        # RAII TCP socket wrappers
│   └── ui/
│       └── ui_app.h/cpp         # SDL2/ImGui application (optional)
└── tests/
    ├── test_aerodynamics.cpp
    ├── test_atmosphere.cpp
    ├── test_config.cpp
    ├── test_engine.cpp
    ├── test_flight_dynamics.cpp
    ├── test_input.cpp
    ├── test_math.cpp
    ├── test_message_codec.cpp
    ├── test_network.cpp
    ├── test_simulation_engine.cpp
    └── test_wind_model.cpp
```

## Testing

Tests use [Google Test](https://github.com/google/googletest) v1.15.2, fetched automatically via CMake FetchContent.

```bash
# Build with tests
cmake -S . -B build -DLUFT_BUILD_TESTS=ON
cmake --build build -j$(nproc)

# Run all tests via CTest
cd build && ctest --output-on-failure

# Or run the test binary directly for verbose output
./build/luft_tests
```

The test suite covers every core module: atmosphere, aerodynamics, flight dynamics, engine model, wind model, configuration parsing, input management, math utilities, message codec, networking, and the top-level simulation engine.

## Coverage

Code coverage requires `lcov` and `genhtml`. A helper script automates the full workflow:

```bash
./scripts/coverage.sh
```

This will:

1. Configure a coverage-instrumented build in `build-coverage/`
2. Build the project and test suite
3. Run all tests
4. Collect coverage data with `lcov`
5. Generate an HTML report at `build-coverage/coverage_html/index.html`

Test files, Google Test internals, and system headers are excluded from the report.

## Architecture

Luft is organized into three layers:

**Core library (`luft_core`)** -- The simulation kernel, built as a static library. Contains the physics models (atmosphere, aerodynamics, engine, wind), the RK4 flight dynamics integrator, the simulation engine that orchestrates per-tick updates, configuration and logging infrastructure, and the TCP networking subsystem.

**UI library (`luft_ui`)** -- An optional static library providing a real-time SDL2/OpenGL window with Dear ImGui panels for telemetry, flight controls, simulation state management, and log output. Built only when `LUFT_BUILD_UI=ON`.

**Driver executable (`luft_sim`)** -- The application entry point. Parses command-line arguments, loads configuration, wires together the simulation engine, network service, and (optionally) UI, then runs the main loop. The main loop steps the simulation forward in lock-step with wall-clock time, publishes telemetry at a configurable rate, and renders UI frames when enabled.

### Threading model

- **Main thread** -- Runs the simulation loop (fixed-step RK4 integration) and, when enabled, the SDL2/ImGui render loop.
- **Network thread** -- Runs an epoll-based event loop that accepts TCP clients, receives commands and control inputs, and broadcasts telemetry packets. Communication with the main thread uses atomic state and callbacks.

### Networking

Luft exposes two TCP ports (configurable, default 5000/5001):

- **Telemetry port (5000)** -- Accepts connections and streams serialized aircraft state at a configurable rate (default 20 Hz).
- **Command port (5001)** -- Accepts connections and receives simulation commands (start, pause, resume, reset, stop) and control inputs (elevator, aileron, rudder, throttle).

Graceful shutdown is handled via SIGINT/SIGTERM signal handlers that set an atomic quit flag, causing both the main loop and the network thread to exit cleanly.

## License

This project is licensed under the [MIT License](https://opensource.org/licenses/MIT).
