# Smart Order Router (SOR)

Production-grade Smart Order Router in C++20 with Python simulation tooling.

Routes orders across multiple venues using configurable strategies, real-time market data aggregation, per-venue rate limiting, and ZeroMQ-based IPC.

## Architecture

```
┌──────────────┐     ┌───────────────┐     ┌──────────────┐
│  ZMQ Gateway │────▶│ Routing Engine │────▶│    Venues    │
│  (REQ/REP)   │     │               │     │  NYSE,NASDAQ │
└──────────────┘     │  BestPrice    │     │  BATS,IEX    │
                     │  LiqSweep     │     └──────┬───────┘
┌──────────────┐     │  SmartIOC     │            │
│  Market Data │────▶│  VWAP         │     ┌──────▼───────┐
│  PUB/SUB     │     └───────┬───────┘     │  Execution   │
└──────────────┘             │             │  Handler     │
                     ┌───────▼───────┐     └──────┬───────┘
                     │ Risk Manager  │            │
                     │  Rate Limiter │     ┌──────▼───────┐
                     │  Kill Switch  │     │  ZMQ PUB     │
                     └───────────────┘     │  (fills/md)  │
                                           └──────────────┘
```

## Quick Start

### Build

```bash
cd sor
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Dependencies are fetched automatically via CMake FetchContent:
- **spdlog** — structured logging
- **nlohmann/json** — JSON serialization
- **yaml-cpp** — YAML config parsing
- **prometheus-cpp** — metrics endpoint
- **cppzmq / libzmq** — ZeroMQ IPC
- **Catch2 v3** — testing framework

### Run

```bash
# Simulation mode (built-in defaults)
./build/sor_app

# With YAML config
./build/sor_app config/sor_config.yaml
```

### Test

```bash
# C++ unit tests (120 test cases, 10700+ assertions)
./build/tests/unit/sor_unit_tests

# C++ integration tests (15 test cases)
./build/tests/integration/sor_integration_tests

# Python tests (66 tests)
cd python && pip install -r requirements.txt && python -m pytest tests/ -v
```

## Configuration

All runtime behavior is config-driven. See `config/sor_config.yaml` for the full reference.

Key sections:

| Section | Controls |
|---------|----------|
| `venues` | Venue list with adapter type, fees, rate limits |
| `routing` | Default strategy, per-strategy tuning |
| `risk` | Order limits, kill switch, rate limiter |
| `gateway.api` | ZMQ endpoints (order, market data, execution) |
| `metrics.prometheus` | Prometheus pull endpoint |

Config is validated at startup — the app fails fast with clear messages on invalid configuration.

## Routing Strategies

| Strategy | Description |
|----------|-------------|
| **BestPrice** | Routes entire order to the venue with the best available price |
| **LiquiditySweep** | Splits order across multiple venues proportional to available liquidity |
| **SmartIOC** | Immediate-or-cancel with intelligent venue selection |
| **VWAP** | Time-sliced execution targeting volume-weighted average price |

## ZMQ Transport

When `gateway.api.enabled: true`:

- **tcp://*:5555** — REQ/REP for order submission and status queries (JSON)
- **tcp://*:5556** — PUB/SUB for real-time NBBO market data updates
- **tcp://*:5557** — PUB/SUB for execution events (fills, completions)

## Project Structure

```
sor/
├── app/main.cpp          # End-to-end pipeline executable
├── config/               # YAML configuration files
├── scripts/              # Coverage and utility scripts
├── core/                 # Types, Order, MemoryPool, FixedPoint
├── routing/              # Strategy implementations
├── market_data/          # Aggregator, FeedHandler, OrderBook
├── connectors/           # SimulatedExchange, FIX adapter
├── execution/            # ExecutionHandler, FillManager
├── gateway/              # ZMQ transport, API gateway
├── risk/                 # RiskManager, RateLimiter, KillSwitch
├── state/                # Order state machine
├── infra/                # Config, Logging, Metrics, Tracing
├── tests/
│   ├── unit/             # 120 unit tests
│   └── integration/      # 15 integration tests
└── python/
    ├── simulation/       # OrderBook, MarketSimulator, MockVenue
    ├── backtesting/      # ReplayEngine, StrategyEvaluator, ScenarioRunner
    ├── tools/            # OrderGenerator, FIX builder, LatencyAnalyzer
    └── tests/            # 66 pytest tests
```

## Benchmarking

Run the app in simulation mode to get end-to-end performance numbers:

```bash
./build/sor_app config/sor_config.yaml
```

The simulation output includes:
- Per-order routing decisions and fill states
- Per-venue fill/reject/cancel statistics and latency
- Routing engine throughput (orders routed, slices generated)
- ZMQ transport publish counts

## Code Coverage

### Prerequisites

- **gcov** (included with GCC)
- **lcov** — `sudo apt install lcov`
- **genhtml** (included with lcov)

### Quick Run

```bash
./scripts/run_coverage.sh
```

This builds with coverage, runs all tests, and generates an HTML report at `build_coverage/coverage/index.html`.

### Manual Steps

```bash
# 1. Configure with coverage enabled
cmake -B build_coverage -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON

# 2. Build
cmake --build build_coverage -j$(nproc)

# 3. Run tests + collect coverage
cd build_coverage
make coverage

# 4. Generate HTML report
make coverage_html

# 5. View
xdg-open coverage/index.html
```

### Interpreting Reports

The HTML report shows line and branch coverage per source file. Coverage includes:
- Routing logic (strategy dispatch, order validation)
- Order state machine (transitions, events)
- Risk manager (limit checks, kill switch, rate limiter)
- Market data processing (aggregation, NBBO, feeds)
- Execution handling (fills, reroutes, child-order tracking)

Third-party code, system headers, and test code are excluded.

## Logging

The SOR uses **spdlog** with dual sinks: console (color) and rotating file.

### Configuration

In `config/sor_config.yaml`:

```yaml
system:
  log_level: "info"           # trace, debug, info, warn, error, critical
  log_file: "logs/sor.log"    # relative or absolute path; directory auto-created
```

If no config file is provided, logs default to `logs/sor.log` in the working directory.

### Log Format

```
[2026-01-01 12:00:00.123456] [thread 1234] [info] [RoutingEngine] Routed order 42 -> 3 slices
```

Each line includes: timestamp (microsecond precision), thread ID, log level, component tag, and message.

### Log Levels

| Level | Usage |
|-------|-------|
| `trace` | Fine-grained execution flow |
| `debug` | Routing decisions, fill details, child order tracking |
| `info` | Startup, shutdown, venue connections, simulation milestones |
| `warn` | Rate limiting, risk rejections, reroutes |
| `error` | Connection failures, config errors |
| `critical` | Kill switch activation, no venues available |

### File Rotation

- Max file size: 10 MB
- Max backup files: 5
- Flush policy: immediate flush on `warn` and above

### Log Directory

The `logs/` directory is auto-created if missing. If the directory is not writable, the app continues with console-only logging and prints a warning to stderr.

## Extending

**Add a new routing strategy:**
1. Create `routing/my_strategy.h/.cpp` implementing `routing::RoutingStrategy`
2. Add the source file to `CMakeLists.txt`
3. Register in `main.cpp`: `router.register_strategy(std::make_unique<MyStrategy>())`

**Add a new venue connector:**
1. Implement `connectors::VenueAdapter` interface
2. Wire into the venue connection loop in `main.cpp`

**Python simulation:**
```python
from simulation import MarketSimulator, VenueConfig

sim = MarketSimulator("AAPL", [
    VenueConfig(1, "NYSE", fee_rate=0.001),
    VenueConfig(2, "NASDAQ", fee_rate=0.0008),
])
sim.populate_books()
routed = sim.route_order(Side.BUY, 100, strategy="best_price")
```
