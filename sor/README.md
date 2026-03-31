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
