# Smart Order Router -- Developer Guide

## 1. Prerequisites

| Requirement         | Minimum Version | Notes                                  |
|---------------------|-----------------|----------------------------------------|
| **C++ Compiler**    | GCC 11+ or Clang 14+ | Must support C++20 (`-std=c++20`). Concepts, `consteval`, spaceship operator, `std::chrono` improvements are used. |
| **CMake**           | 3.20+           | Required for `FetchContent` and modern target-based configuration. |
| **Python**          | 3.10+           | For simulation, backtesting, and developer tools. |
| **Git**             | 2.25+           | CMake `FetchContent` fetches dependencies at configure time. |

Third-party C++ dependencies are fetched automatically via CMake FetchContent
on the first build. No manual installation is needed for:

- **Catch2** -- unit and integration testing framework
- **spdlog** -- structured logging
- **nlohmann/json** -- JSON parsing for the API gateway
- **yaml-cpp** -- YAML configuration parsing

---

## 2. Building

### 2.1 Release Build

```bash
cd /path/to/sor
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DSOR_BUILD_TESTS=ON
cmake --build . -j$(nproc)
```

The Release build enables `-O3 -march=native -DNDEBUG` for maximum
performance on the host CPU.

### 2.2 Debug Build

```bash
cd /path/to/sor
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug -DSOR_BUILD_TESTS=ON
cmake --build . -j$(nproc)
```

### 2.3 Debug Build with Sanitizers

```bash
cd /path/to/sor
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug \
         -DSOR_BUILD_TESTS=ON \
         -DSOR_USE_SANITIZERS=ON
cmake --build . -j$(nproc)
```

This enables AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan).
Run tests under sanitizers to detect memory errors and undefined behavior.

### 2.4 CMake Options

| Option              | Default | Description                                 |
|---------------------|---------|---------------------------------------------|
| `SOR_BUILD_TESTS`   | `ON`    | Build unit and integration tests.           |
| `SOR_BUILD_PYTHON`  | `OFF`   | Build Python bindings (pybind11).           |
| `SOR_USE_SANITIZERS` | `OFF`  | Enable ASan + UBSan.                        |

### 2.5 Build Artifacts

After a successful build, the following library targets are produced:

| Target             | Directory       | Description                          |
|--------------------|-----------------|--------------------------------------|
| `sor_core`         | `core/`         | Types, order model, queues, pool     |
| `sor_routing`      | `routing/`      | Routing engine + strategies          |
| `sor_market_data`  | `market_data/`  | Order book, aggregator, feeds        |
| `sor_connectors`   | `connectors/`   | Venue adapters                       |
| `sor_gateway`      | `gateway/`      | FIX and API gateways                 |
| `sor_risk`         | `risk/`         | Risk manager, rate limiter, kill switch |
| `sor_state`        | `state/`        | Order state machine                  |
| `sor_execution`    | `execution/`    | Execution handler, fill manager      |
| `sor_infra`        | `infra/`        | Logging, config, metrics, tracing    |
| `sor_utils`        | `utils/`        | String and time utilities            |

---

## 3. Running Tests

### 3.1 Run All Tests

```bash
cd build
ctest --output-on-failure
```

### 3.2 Run Unit Tests Only

```bash
cd build
./tests/unit/sor_unit_tests
```

### 3.3 Run Integration Tests Only

```bash
cd build
./tests/integration/sor_integration_tests
```

### 3.4 Run a Specific Test

Catch2 supports test name filtering via command-line arguments:

```bash
# Run all tests matching a pattern
./tests/unit/sor_unit_tests "OrderStateMachine*"
./tests/unit/sor_unit_tests "[fixed_point]"

# List all available tests
./tests/unit/sor_unit_tests --list-tests
```

### 3.5 Test Coverage

| Test File                         | Coverage Area                          |
|-----------------------------------|----------------------------------------|
| `test_fixed_point.cpp`            | Fixed-point arithmetic, edge cases     |
| `test_memory_pool.cpp`            | Pool alloc/dealloc, exhaustion, threads |
| `test_market_data.cpp`            | Book updates, NBBO, aggregation        |
| `test_order_state_machine.cpp`    | All valid transitions, invalid rejections |
| `test_risk_manager.cpp`           | Each risk check, kill switch, rate limiter |
| `test_routing_strategies.cpp`     | Each strategy with known inputs/outputs |
| `test_order_lifecycle.cpp`        | End-to-end order flow                  |
| `test_partial_fills.cpp`          | Multi-slice partial fill aggregation   |
| `test_venue_failover.cpp`         | Child rejection and rerouting          |

---

## 4. Project Structure

```
sor/
+-- CMakeLists.txt              # Top-level build configuration
+-- core/
|   +-- CMakeLists.txt
|   +-- types.h / types.cpp     # Price, Quantity, OrderId, enums, FixedString<N>
|   +-- order.h / order.cpp     # Order, ExecutionReport, CancelRequest
|   +-- fixed_point.h / .cpp    # FixedPoint<Scale> template
|   +-- memory_pool.h / .cpp    # Lock-free Treiber-stack object pool
|   +-- spsc_queue.h            # SPSC bounded ring buffer
|   +-- mpsc_queue.h            # MPSC bounded queue (Vyukov-style)
+-- gateway/
|   +-- CMakeLists.txt
|   +-- gateway.h / .cpp        # Main gateway, order/exec processing loops
|   +-- api_gateway.h / .cpp    # JSON API adapter
|   +-- fix_gateway.h / .cpp    # Simplified FIX protocol adapter
+-- risk/
|   +-- CMakeLists.txt
|   +-- risk_manager.h / .cpp   # Pre-trade risk checks, position tracking
|   +-- rate_limiter.h / .cpp   # Sliding-window rate limiter
|   +-- kill_switch.h / .cpp    # Global circuit breaker
+-- routing/
|   +-- CMakeLists.txt
|   +-- engine.h / .cpp         # RoutingEngine orchestrator
|   +-- strategy.h              # Abstract base + RoutingDecision, VenueScore
|   +-- best_price.h / .cpp     # BestPrice strategy
|   +-- liquidity_sweep.h / .cpp # LiquiditySweep strategy
|   +-- smart_ioc.h / .cpp      # SmartIOC strategy
|   +-- vwap.h / .cpp           # VWAP strategy
+-- market_data/
|   +-- CMakeLists.txt
|   +-- book.h / .cpp           # L2 OrderBook (per-venue)
|   +-- aggregator.h / .cpp     # NBBO + aggregated depth across venues
|   +-- feed_handler.h / .cpp   # Abstract feed + SimulatedFeedHandler
|   +-- replay_engine.h / .cpp  # Historical tick replay
+-- connectors/
|   +-- CMakeLists.txt
|   +-- venue_adapter.h / .cpp  # Abstract VenueAdapter interface
|   +-- simulated_exchange.h / .cpp # In-process matching engine
|   +-- fix_adapter.h / .cpp    # Mock FIX 4.4 adapter
+-- execution/
|   +-- CMakeLists.txt
|   +-- execution_handler.h / .cpp  # Parent/child tracking, fill propagation
|   +-- fill_manager.h / .cpp       # Fill recording, indexing, aggregation
+-- state/
|   +-- CMakeLists.txt
|   +-- order_state_machine.h / .cpp # Compile-time transition table
+-- infra/
|   +-- CMakeLists.txt
|   +-- logging.h / .cpp        # spdlog wrapper + SOR_LOG_* macros
|   +-- config.h / .cpp         # YAML config with hot reload
|   +-- metrics.h / .cpp        # Prometheus-style metrics
|   +-- tracing.h / .cpp        # Per-order lifecycle tracing
+-- utils/
|   +-- CMakeLists.txt
|   +-- string_utils.h / .cpp   # String formatting helpers
|   +-- time_utils.h / .cpp     # Timestamp formatting and conversion
+-- tests/
|   +-- CMakeLists.txt
|   +-- unit/
|   |   +-- CMakeLists.txt
|   |   +-- test_fixed_point.cpp
|   |   +-- test_memory_pool.cpp
|   |   +-- test_market_data.cpp
|   |   +-- test_order_state_machine.cpp
|   |   +-- test_risk_manager.cpp
|   |   +-- test_routing_strategies.cpp
|   +-- integration/
|       +-- CMakeLists.txt
|       +-- test_order_lifecycle.cpp
|       +-- test_partial_fills.cpp
|       +-- test_venue_failover.cpp
+-- python/
|   +-- simulation/
|   |   +-- __init__.py
|   |   +-- market_simulator.py
|   |   +-- mock_venue.py
|   |   +-- order_book.py
|   +-- backtesting/
|   |   +-- __init__.py
|   |   +-- replay_engine.py
|   |   +-- scenario_runner.py
|   |   +-- strategy_evaluator.py
|   +-- tools/
|       +-- __init__.py
|       +-- order_generator.py
|       +-- fix_message_builder.py
|       +-- latency_analyzer.py
+-- third_party/
|   +-- CMakeLists.txt          # FetchContent for Catch2, spdlog, etc.
+-- docs/
    +-- system.md               # Business problem and market microstructure
    +-- architecture.md         # System architecture
    +-- developer_guide.md      # This file
    +-- api_reference.md        # Class and endpoint reference
```

---

## 5. How to Add a New Routing Strategy

### Step 1: Create the Header

Create `routing/my_strategy.h`:

```cpp
#pragma once

#include "routing/strategy.h"

namespace sor::routing {

class MyStrategy final : public RoutingStrategy {
public:
    RoutingDecision route(const Order& order,
                          const market_data::NBBO& nbbo,
                          const market_data::AggregatedBook& book,
                          const std::vector<VenueScore>& venues) override;

    const char* name() const noexcept override { return "MyStrategy"; }

    // Return the RoutingStrategy enum tag for dispatch.
    // You will need to add a new entry to the RoutingStrategy enum
    // in core/types.h if this is a new strategy type.
    sor::RoutingStrategy type() const noexcept override {
        return sor::RoutingStrategy::BestPrice;  // replace with your enum
    }
};

} // namespace sor::routing
```

### Step 2: Implement the Strategy

Create `routing/my_strategy.cpp`:

```cpp
#include "routing/my_strategy.h"
#include "market_data/aggregator.h"

namespace sor::routing {

RoutingDecision MyStrategy::route(
    const Order& order,
    const market_data::NBBO& nbbo,
    const market_data::AggregatedBook& book,
    const std::vector<VenueScore>& venues)
{
    RoutingDecision decision;

    // Your routing logic here.
    // Populate decision.slices with one or more Slice entries:
    //   { venue_id, price, quantity, type, tif }

    return decision;
}

} // namespace sor::routing
```

### Step 3: Add the Enum Value (if new strategy type)

In `core/types.h`, add a new entry to the `RoutingStrategy` enum:

```cpp
enum class RoutingStrategy : uint8_t {
    BestPrice = 0,
    LiquiditySweep = 1,
    SmartIOC = 2,
    VWAP = 3,
    MyStrategy = 4,   // <-- add this
};
```

### Step 4: Register in the RoutingEngine

In your initialization code (e.g., `Gateway::initialize()`), register the
strategy:

```cpp
routing_engine_->register_strategy(
    std::make_unique<routing::MyStrategy>());
```

### Step 5: Update CMakeLists.txt

In `routing/CMakeLists.txt`, add the new source file:

```cmake
target_sources(sor_routing PRIVATE
    # ... existing sources ...
    my_strategy.cpp
)
```

### Step 6: Write Tests

Add test cases to `tests/unit/test_routing_strategies.cpp`:

```cpp
TEST_CASE("MyStrategy routes correctly", "[routing][my_strategy]") {
    MyStrategy strategy;
    Order order;
    // Set up order, NBBO, book, and venue scores...
    auto decision = strategy.route(order, nbbo, book, venues);
    REQUIRE(decision.valid());
    // Assert on expected slices...
}
```

---

## 6. How to Add a New Venue

### Step 1: Create the Adapter

Create `connectors/my_venue_adapter.h` and `connectors/my_venue_adapter.cpp`.
Inherit from `VenueAdapter`:

```cpp
#pragma once

#include "connectors/venue_adapter.h"

namespace sor::connectors {

class MyVenueAdapter : public VenueAdapter {
public:
    struct Config {
        VenueId venue_id{10};
        std::string name{"MyVenue"};
        std::string endpoint{"tcp://venue.example.com:9000"};
        std::chrono::microseconds timeout{5000};
    };

    explicit MyVenueAdapter(Config config);

    // -- VenueAdapter interface --
    bool connect() override;
    void disconnect() override;
    [[nodiscard]] bool is_connected() const override;

    bool send_order(const Order& order) override;
    bool cancel_order(const CancelRequest& request) override;

    [[nodiscard]] VenueId venue_id() const override;
    [[nodiscard]] const char* venue_name() const override;
    [[nodiscard]] VenueStatus status() const override;
    [[nodiscard]] std::chrono::microseconds avg_latency() const override;

private:
    Config config_;
    std::atomic<bool> connected_{false};
};

} // namespace sor::connectors
```

Key implementation requirements:

- `send_order()` must translate the `Order` to the venue's wire protocol
  and transmit it. Return `true` if the order was accepted for transmission.
- On receiving execution reports from the venue, translate them to
  `ExecutionReport` and call `notify_execution(report)` (inherited from
  `VenueAdapter`). This dispatches the report to the registered callback.
- `cancel_order()` must send a cancel request to the venue.
- `avg_latency()` should return a rolling average round-trip latency.

### Step 2: Register with the Gateway

```cpp
auto venue = std::make_unique<connectors::MyVenueAdapter>(
    connectors::MyVenueAdapter::Config{
        .venue_id = 10,
        .name = "MyVenue",
        .endpoint = "tcp://venue.example.com:9000"
    });
gateway.add_venue(std::move(venue));
```

### Step 3: Add Venue Configuration

Add an entry to the YAML config file:

```yaml
venues:
  - venue_id: 10
    name: "MyVenue"
    type: "my_venue"
    endpoint: "tcp://venue.example.com:9000"
    fee_rate: 0.0012
    enabled: true
    max_orders_per_second: 200
```

### Step 4: Update CMakeLists.txt

In `connectors/CMakeLists.txt`:

```cmake
target_sources(sor_connectors PRIVATE
    # ... existing sources ...
    my_venue_adapter.cpp
)
```

### Step 5: Write Tests

Create integration tests that exercise the full order lifecycle through your
new venue adapter, including fill, partial fill, reject, and cancel scenarios.

---

## 7. Configuration

The SOR is configured via a YAML file loaded by `ConfigManager`.

### 7.1 YAML Structure

```yaml
# ---- Logging ----
log_level: "info"            # trace, debug, info, warn, error, critical
log_file: ""                 # empty = console only

# ---- Metrics ----
enable_metrics: true
metrics_port: 9090           # Prometheus HTTP endpoint port

# ---- Data ----
data_dir: ""                 # directory for market data files

# ---- Venues ----
venues:
  - venue_id: 1
    name: "SimExchange"
    type: "simulated"        # "simulated" or "fix"
    endpoint: ""
    fee_rate: 0.001          # 10 bps taker fee
    enabled: true
    max_orders_per_second: 100

  - venue_id: 2
    name: "FixVenue"
    type: "fix"
    endpoint: "tcp://localhost:9876"
    fee_rate: 0.0008
    enabled: true
    max_orders_per_second: 200

# ---- Strategy Tuning ----
strategy:
  default_strategy: "BestPrice"    # BestPrice, LiquiditySweep, SmartIOC, VWAP
  vwap_num_slices: 10              # number of time slices for VWAP
  vwap_duration_seconds: 300       # target duration in seconds
  vwap_urgency: 0.5               # 0.0 = passive, 1.0 = aggressive
  sweep_min_slice: 10              # minimum quantity per sweep slice
  ioc_slippage_ticks: 2           # max ticks beyond NBBO for SmartIOC

# ---- Risk Limits ----
risk:
  max_order_quantity: 10000        # max shares per order (0 = unlimited)
  max_order_notional: 0            # max notional per order (0 = unlimited)
  max_position_notional: 0         # max position notional per symbol
  max_position_quantity: 0         # max position quantity per symbol
  max_orders_per_second: 100       # rate limit
  max_open_orders: 1000            # max concurrent open orders
  max_loss: 0                      # max P&L loss threshold (0 = unlimited)
```

### 7.2 Hot Reload

The `ConfigManager` supports hot reload. Call `check_for_changes()` from a
background timer thread:

```cpp
// In a background thread or timer callback:
sor::infra::ConfigManager::instance().check_for_changes();
```

This compares the file's modification time and reloads if it has changed.
Registered change callbacks are invoked with the new `SystemConfig`.

### 7.3 Programmatic Access

```cpp
auto& config = sor::infra::ConfigManager::instance();
config.load("config/sor_config.yaml");

// Typed access to the full config struct:
auto sys = config.get_config();
std::cout << "Log level: " << sys.log_level << "\n";
std::cout << "Venues: " << sys.venues.size() << "\n";

// Flat key-value access:
int rate = config.get_int("risk.max_orders_per_second", 100);
double urgency = config.get_double("strategy.vwap_urgency", 0.5);
```

---

## 8. Python Tools

### 8.1 Order Generator

Generates synthetic orders for testing and benchmarking.

```bash
python -m python.tools.order_generator --count 1000
```

Programmatic usage:

```python
from python.tools.order_generator import generate_orders

orders = generate_orders(num_orders=1000, mid_price=150.0)
for order in orders[:5]:
    print(order)
```

### 8.2 FIX Message Builder

Constructs FIX protocol messages for testing the FIX gateway.

```bash
python -m python.tools.fix_message_builder --type new_order
```

Programmatic usage:

```python
from python.tools.fix_message_builder import FixMessageBuilder

builder = FixMessageBuilder()
msg = builder.new_order_single(
    cl_ord_id="ORD001",
    symbol="AAPL",
    side="buy",
    quantity=100,
    price=150.25
)
print(repr(msg))
```

### 8.3 Latency Analyzer

Analyzes trace output files to compute latency statistics.

```bash
python -m python.tools.latency_analyzer traces.json
```

Programmatic usage:

```python
from python.tools.latency_analyzer import LatencyAnalyzer

analyzer = LatencyAnalyzer()
analyzer.load("traces.json")
# Or generate synthetic data for testing:
analyzer.generate_synthetic(num_orders=10000)
analyzer.print_report()
```

### 8.4 Market Simulator

Simulates multi-venue market data for backtesting.

```python
from python.simulation.market_simulator import MarketSimulator, VenueConfig

venues = [
    VenueConfig(1, "NYSE", 0.001, 50),
    VenueConfig(2, "NASDAQ", 0.0008, 30),
]
sim = MarketSimulator("AAPL", venues)
sim.generate_market_data(num_ticks=1000)
print(sim.get_nbbo())
```

### 8.5 Backtesting

Run parameterized scenarios against the routing strategies.

```python
from python.backtesting.scenario_runner import ScenarioRunner, basic_buy_scenario

runner = ScenarioRunner()
runner.add(basic_buy_scenario())
results = runner.run_all()
runner.print_report(results)
```

---

## 9. Debugging

### 9.1 Per-Order Tracing

The `Tracer` singleton records a timeline of events for each order. Enable
tracing in your code:

```cpp
#include "infra/tracing.h"

auto& tracer = sor::infra::Tracer::instance();

// Start tracing when an order is submitted:
tracer.begin_trace(order.id);

// Record events at each stage:
tracer.trace(order.id, "risk_check", "passed");
tracer.trace(order.id, "route", "strategy=BestPrice, venue=1");
tracer.trace(order.id, "venue_send", "venue_id=1");
tracer.trace(order.id, "fill", "qty=100, price=150.04");

// End tracing when the order reaches a terminal state:
tracer.end_trace(order.id);

// Dump the trace for debugging:
std::string trace_str = tracer.dump_trace(order.id);
SOR_LOG_INFO("Order trace:\n{}", trace_str);
```

### 9.2 Log Levels

The logging system supports six levels, configurable at runtime:

| Level      | Use Case                                              |
|------------|-------------------------------------------------------|
| `Trace`    | Per-tick market data, queue push/pop, state transitions. Very verbose. |
| `Debug`    | Routing decisions, risk check details, venue scores.  |
| `Info`     | Order submissions, fills, system startup/shutdown.    |
| `Warn`     | Stale market data, venue degraded, nearing rate limit. |
| `Error`    | Order rejections, venue disconnects, state machine violations. |
| `Critical` | Kill switch activation, unrecoverable errors.         |

Change the log level at runtime:

```cpp
sor::infra::Logger::instance().set_level(sor::infra::LogLevel::Debug);
```

Or via the YAML config:

```yaml
log_level: "debug"
```

### 9.3 Metrics

Use the `SOR_METRICS_TIMER` macro for automatic latency measurement:

```cpp
#include "infra/metrics.h"

void process_order(Order& order) {
    SOR_METRICS_TIMER("order_processing_latency_us");
    // ... processing code ...
}
```

Query metrics programmatically or scrape the Prometheus endpoint:

```bash
curl http://localhost:9090/metrics
```

---

## 10. Performance Tuning

### 10.1 Compiler Flags

The Release build automatically enables `-O3 -march=native`. For additional
tuning:

```cmake
# Link-time optimization
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON

# Profile-guided optimization (two-pass)
# Pass 1: instrument
cmake .. -DCMAKE_CXX_FLAGS="-fprofile-generate"
# Run representative workload
# Pass 2: optimize
cmake .. -DCMAKE_CXX_FLAGS="-fprofile-use"
```

### 10.2 CPU Pinning

Pin the order processing thread to a dedicated CPU core to avoid context
switches and cache pollution:

```cpp
#include <pthread.h>
#include <sched.h>

void pin_to_core(std::thread& t, int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(t.native_handle(), sizeof(cpuset), &cpuset);
}

// After gateway.start():
pin_to_core(order_thread_, 2);   // pin to core 2
pin_to_core(exec_thread_, 3);    // pin to core 3
```

### 10.3 NUMA Awareness

On multi-socket systems, ensure the order processing thread's memory is
allocated on the same NUMA node as its CPU core:

```bash
# Run the process on NUMA node 0:
numactl --cpunodebind=0 --membind=0 ./sor_main

# Or use libnuma programmatically
```

### 10.4 Kernel Tuning

For lowest latency:

```bash
# Isolate CPUs from the kernel scheduler
# (add to kernel boot params: isolcpus=2,3)

# Disable CPU frequency scaling
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Disable transparent huge pages (can cause latency spikes)
echo never | sudo tee /sys/kernel/mm/transparent_hugepage/enabled
```

### 10.5 Queue Sizing

The default queue capacities (8192 for orders and reports, 4096 for cancels)
are suitable for most workloads. If you observe queue-full rejections under
load, increase the template parameter in `Gateway`:

```cpp
// In gateway.h, adjust the template parameters:
MPSCQueue<Order, 16384> order_queue_;       // was 8192
SPSCQueue<ExecutionReport, 16384> report_queue_;  // was 8192
```

Queue capacities must be powers of two.

---

## 11. Coding Standards

- **Language**: C++20 with `-Wall -Wextra -Wpedantic`.
- **No raw `new`/`delete`**: use `MemoryPool`, `std::unique_ptr`, or stack allocation.
- **No heap allocation on hot path**: use `FixedString`, pre-allocated pools,
  and fixed-size arrays.
- **Namespaces**: `sor::<module>` (e.g., `sor::core`, `sor::routing`,
  `sor::market_data`).
- **File naming**: `snake_case.h` / `snake_case.cpp`.
- **Header guards**: `#pragma once`.
- **Documentation**: doxygen-style `///` comments on all public APIs.
- **Thread safety**: document thread safety guarantees in class-level comments.
  Use `[[nodiscard]]` on all query functions.

---

*See also: [System Documentation](system.md) | [Architecture](architecture.md) | [API Reference](api_reference.md)*
