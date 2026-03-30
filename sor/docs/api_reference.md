# Smart Order Router -- API Reference

This document provides a reference for the public interfaces of the key
classes in the SOR system. For architectural context and data flow, see the
[Architecture Documentation](architecture.md).

All classes live in the `sor` namespace, further scoped by module
(e.g., `sor::gateway`, `sor::routing`, `sor::risk`).

---

## Table of Contents

1. [Gateway](#1-gateway)
2. [ApiGateway](#2-apigateway)
3. [FixGateway](#3-fixgateway)
4. [RoutingEngine](#4-routingengine)
5. [RoutingStrategy (Abstract)](#5-routingstrategy-abstract)
6. [RiskManager](#6-riskmanager)
7. [MarketDataAggregator](#7-marketdataaggregator)
8. [ExecutionHandler](#8-executionhandler)
9. [FillManager](#9-fillmanager)
10. [VenueAdapter (Abstract)](#10-venueadapter-abstract)
11. [OrderStateMachine](#11-orderstatemachine)
12. [ConfigManager](#12-configmanager)
13. [MetricsManager](#13-metricsmanager)
14. [Tracer](#14-tracer)

---

## 1. Gateway

**Header:** `gateway/gateway.h`
**Namespace:** `sor::gateway`

The top-level entry point for the Smart Order Router. Owns the processing
loops, subsystem references, and venue connections.

### Construction

```cpp
struct Config {
    size_t      order_queue_size{8192};
    size_t      report_queue_size{8192};
    bool        enable_metrics{true};
    std::string config_path;
};

explicit Gateway(Config config);
```

### Lifecycle

| Method          | Signature                | Description                         |
|-----------------|--------------------------|-------------------------------------|
| `initialize`    | `bool initialize()`      | Initialize all subsystems. Returns true on success. Must be called before `start()`. |
| `start`         | `void start()`           | Start the order processing and execution report processing threads. |
| `stop`          | `void stop()`            | Signal both processing threads to stop and join them. |
| `is_running`    | `bool is_running() const`| Returns true if the processing loops are active. |

### Order Submission (Thread-Safe, Non-Blocking)

| Method          | Signature                              | Description                      |
|-----------------|----------------------------------------|----------------------------------|
| `submit_order`  | `bool submit_order(Order order)`       | Enqueue an order for processing. Returns false if the MPSC queue is full. |
| `cancel_order`  | `bool cancel_order(CancelRequest req)` | Enqueue a cancel request. Returns false if the cancel queue is full. |

### Venue Management

| Method          | Signature                                              | Description              |
|-----------------|--------------------------------------------------------|--------------------------|
| `add_venue`     | `void add_venue(std::unique_ptr<connectors::VenueAdapter> adapter)` | Register a venue adapter. The gateway takes ownership. |

### Order Query

| Method          | Signature                              | Description                      |
|-----------------|----------------------------------------|----------------------------------|
| `get_order`     | `const Order* get_order(OrderId id) const` | Look up an order by ID. Returns nullptr if not found. |

### Subsystem Accessors

| Method              | Return Type                          | Description                |
|---------------------|--------------------------------------|----------------------------|
| `market_data()`     | `market_data::MarketDataAggregator&` | Market data aggregator     |
| `risk_manager()`    | `risk::RiskManager&`                 | Risk manager               |
| `routing_engine()`  | `routing::RoutingEngine&`            | Routing engine             |
| `execution_handler()`| `execution::ExecutionHandler&`      | Execution handler          |
| `fill_manager()`    | `execution::FillManager&`            | Fill manager               |

### Statistics

```cpp
struct Stats {
    uint64_t orders_submitted{0};
    uint64_t orders_routed{0};
    uint64_t orders_completed{0};
    uint64_t orders_rejected{0};
};

Stats get_stats() const;
```

---

## 2. ApiGateway

**Header:** `gateway/api_gateway.h`
**Namespace:** `sor::gateway`

JSON API adapter that translates between JSON strings and the internal order
model.

### Construction

```cpp
explicit ApiGateway(Gateway& gateway);
```

### Endpoints

All methods accept a JSON string and return a JSON response string.

| Method                | Signature                                         | Description              |
|-----------------------|---------------------------------------------------|--------------------------|
| `handle_new_order`    | `std::string handle_new_order(const std::string& json)` | Parse a JSON new-order request, submit to the Gateway. Returns JSON with `order_id` or `error`. |
| `handle_cancel_order` | `std::string handle_cancel_order(const std::string& json)` | Parse a JSON cancel request, submit to the Gateway. Returns JSON with `status`. |
| `handle_query_order`  | `std::string handle_query_order(const std::string& json)` | Query an order by ID. Returns JSON with the order state, filled quantity, and other fields. |
| `handle_status`       | `std::string handle_status(const std::string& json)` | Query gateway status and statistics. Returns JSON with system stats. |

**Example new order JSON request:**

```json
{
    "symbol": "AAPL",
    "side": "buy",
    "type": "limit",
    "price": 150.04,
    "quantity": 100,
    "tif": "IOC",
    "strategy": "BestPrice",
    "client_id": "CLIENT001"
}
```

**Example response:**

```json
{
    "status": "accepted",
    "order_id": 12345
}
```

---

## 3. FixGateway

**Header:** `gateway/fix_gateway.h`
**Namespace:** `sor::gateway`

Simplified FIX protocol adapter for inbound order flow and outbound
execution reports.

### Construction

```cpp
explicit FixGateway(Gateway& gateway);
```

### Methods

| Method              | Signature                                                    | Description              |
|---------------------|--------------------------------------------------------------|--------------------------|
| `on_fix_message`    | `void on_fix_message(const std::string& raw_fix)`           | Parse a raw FIX message and submit to the Gateway. Supports: `35=D` (NewOrderSingle), `35=F` (OrderCancelRequest). |
| `set_send_callback` | `void set_send_callback(std::function<void(const std::string&)> cb)` | Register a callback for outgoing FIX messages (execution reports). |

---

## 4. RoutingEngine

**Header:** `routing/engine.h`
**Namespace:** `sor::routing`

The main orchestrator for routing decisions. Receives orders, performs
validation and risk checks, queries market data, and delegates to the
appropriate strategy.

### Construction

```cpp
RoutingEngine(market_data::MarketDataAggregator& md_aggregator,
              risk::RiskManager& risk_manager);
```

### Order Routing

| Method              | Signature                                    | Description              |
|---------------------|----------------------------------------------|--------------------------|
| `route_order`       | `RoutingDecision route_order(Order& order)`  | Main entry point. Validates the order, runs risk checks, queries NBBO and depth, selects the registered strategy matching `order.strategy`, and returns a `RoutingDecision`. An empty (invalid) decision signals rejection. |

### Strategy Registration

| Method              | Signature                                                  | Description              |
|---------------------|------------------------------------------------------------|--------------------------|
| `register_strategy` | `void register_strategy(std::unique_ptr<RoutingStrategy> strategy)` | Register a routing strategy. Replaces any previous strategy with the same `RoutingStrategy` enum tag. |

### Venue Management

| Method              | Signature                                              | Description              |
|---------------------|--------------------------------------------------------|--------------------------|
| `update_venue_score`| `void update_venue_score(VenueId id, const VenueScore& score)` | Update quality metrics for a venue (latency, fill rate, fees). |
| `remove_venue`      | `void remove_venue(VenueId id)`                        | Remove a venue from the available pool. |

### Callbacks

| Method               | Signature                                                    | Description              |
|----------------------|--------------------------------------------------------------|--------------------------|
| `set_order_callback` | `void set_order_callback(OrderCallback cb)`                  | Called after a successful routing decision. |
| `set_reject_callback`| `void set_reject_callback(RejectCallback cb)`                | Called when an order is rejected (validation or risk failure). |

### Statistics

```cpp
struct Stats {
    uint64_t orders_routed{0};
    uint64_t orders_rejected{0};
    uint64_t total_slices{0};
};

Stats get_stats() const noexcept;
```

---

## 5. RoutingStrategy (Abstract)

**Header:** `routing/strategy.h`
**Namespace:** `sor::routing`

Abstract base class for all routing strategies.

### Virtual Interface

| Method   | Signature                                                                         | Description              |
|----------|-----------------------------------------------------------------------------------|--------------------------|
| `route`  | `virtual RoutingDecision route(const Order&, const NBBO&, const AggregatedBook&, const std::vector<VenueScore>&) = 0` | Produce a routing decision for the given order and market context. |
| `name`   | `virtual const char* name() const noexcept = 0`                                  | Human-readable strategy name for logging. |
| `type`   | `virtual sor::RoutingStrategy type() const noexcept = 0`                          | Enum tag for dispatch by the engine. |

### Concrete Implementations

| Class                    | Enum Tag             | Key Parameters                     |
|--------------------------|----------------------|------------------------------------|
| `BestPriceStrategy`      | `BestPrice`          | None (stateless).                  |
| `LiquiditySweepStrategy` | `LiquiditySweep`     | `min_slice_quantity` (default 1). Max 10 slices. |
| `SmartIOCStrategy`       | `SmartIOC`           | `slippage_ticks` (default 5). Max 10 slices. |
| `VWAPStrategy`           | `VWAP`               | `Config{duration, num_slices, urgency, max_participation_rate}`. |

### Supporting Types

**RoutingDecision:**

```cpp
struct RoutingDecision {
    struct Slice {
        VenueId     venue_id;
        Price       price;
        Quantity    quantity;
        OrderType   type{OrderType::Limit};
        TimeInForce tif{TimeInForce::IOC};
    };

    std::vector<Slice> slices;

    bool valid() const noexcept;            // true if slices non-empty
    Quantity total_quantity() const noexcept; // sum of slice quantities
};
```

**VenueScore:**

```cpp
struct VenueScore {
    VenueId venue_id{0};
    double  latency_us{0.0};
    double  fill_rate{0.0};
    double  fee_rate{0.0};
    Price   fee_adjusted_price{0};
    bool    is_available{true};
};
```

---

## 6. RiskManager

**Header:** `risk/risk_manager.h`
**Namespace:** `sor::risk`

Pre-trade risk management. Every order passes through `check_order()` before
routing.

### Pre-Trade Check

| Method         | Signature                                      | Description              |
|----------------|------------------------------------------------|--------------------------|
| `check_order`  | `RiskCheckResult check_order(const Order& order)` | Run all risk checks in order. Returns `Passed` or the first failing check. |

**RiskCheckResult enum values:**

```
Passed, FailedMaxOrderQuantity, FailedMaxOrderNotional,
FailedMaxPositionNotional, FailedMaxPositionQuantity,
FailedMaxOrdersPerSecond, FailedMaxOpenOrders, FailedMaxLoss,
FailedKillSwitch, FailedRateLimit, FailedSymbolNotAllowed
```

### Limit Configuration

| Method              | Signature                                              | Description              |
|---------------------|--------------------------------------------------------|--------------------------|
| `set_global_limits` | `void set_global_limits(const RiskLimits& limits)`     | Set global (all-symbol) risk limits. |
| `set_symbol_limits` | `void set_symbol_limits(const Symbol& sym, const RiskLimits& limits)` | Set per-symbol risk limits. Overrides global for that symbol. |

**RiskLimits struct:**

```cpp
struct RiskLimits {
    Quantity max_order_quantity{0};
    Price    max_order_notional{0};
    Price    max_position_notional{0};
    Quantity max_position_quantity{0};
    int32_t  max_orders_per_second{0};
    int32_t  max_open_orders{0};
    Price    max_loss{0};
    bool     enabled{true};
};
```

A zero value for any limit field means "not enforced."

### Position / Order Event Updates

| Method              | Signature                                                    | Description              |
|---------------------|--------------------------------------------------------------|--------------------------|
| `on_fill`           | `void on_fill(const Symbol&, Side, Quantity, Price)`         | Update position on fill. |
| `on_order_accepted` | `void on_order_accepted(const Order&)`                       | Increment open order count and pending quantity. |
| `on_order_canceled` | `void on_order_canceled(const Order&)`                       | Decrement open order count and pending quantity. |
| `on_order_rejected` | `void on_order_rejected(const Order&)`                       | Decrement open order count and pending quantity. |

### Position Query

| Method         | Signature                                              | Description              |
|----------------|--------------------------------------------------------|--------------------------|
| `get_position` | `PositionInfo get_position(const Symbol& sym) const`   | Return current position snapshot for a symbol. |

**PositionInfo struct:**

```cpp
struct PositionInfo {
    Quantity net_quantity{0};
    Price    avg_entry_price{0};
    Price    realized_pnl{0};
    Price    unrealized_pnl{0};
    int32_t  open_order_count{0};
    Quantity pending_buy_quantity{0};
    Quantity pending_sell_quantity{0};
};
```

### Kill Switch

| Method                  | Signature                            | Description              |
|-------------------------|--------------------------------------|--------------------------|
| `activate_kill_switch`  | `void activate_kill_switch()`        | Activate the global kill switch. All subsequent `check_order()` calls will return `FailedKillSwitch`. |
| `deactivate_kill_switch`| `void deactivate_kill_switch()`      | Deactivate the kill switch (re-enable trading). |
| `is_kill_switch_active` | `bool is_kill_switch_active() const` | Check if the kill switch is active (atomic, lock-free). |

### Rate Limit

| Method            | Signature                     | Description              |
|-------------------|-------------------------------|--------------------------|
| `check_rate_limit`| `bool check_rate_limit()`     | Check if the current request is within the rate limit. |

### Utility

| Method      | Signature                                             | Description              |
|-------------|-------------------------------------------------------|--------------------------|
| `to_string` | `static const char* to_string(RiskCheckResult result)` | Human-readable name for a risk check result. |

---

## 7. MarketDataAggregator

**Header:** `market_data/aggregator.h`
**Namespace:** `sor::market_data`

Maintains per-venue order books and computes the NBBO and aggregated depth
across all venues.

### Venue Registration

| Method           | Signature                             | Description              |
|------------------|---------------------------------------|--------------------------|
| `register_venue` | `void register_venue(VenueId id)`     | Register a venue for book tracking. |

### Book Updates

| Method           | Signature                                                              | Description              |
|------------------|------------------------------------------------------------------------|--------------------------|
| `on_book_update` | `void on_book_update(VenueId id, const Symbol& sym, const OrderBook& book)` | Update a venue's book for a symbol. Triggers NBBO recomputation. |

### Market Data Queries

| Method                 | Signature                                                  | Description              |
|------------------------|------------------------------------------------------------|--------------------------|
| `get_nbbo`             | `NBBO get_nbbo(const Symbol& sym) const`                  | Get the current NBBO for a symbol across all venues. |
| `get_aggregated_book`  | `AggregatedBook get_aggregated_book(const Symbol& sym) const` | Get merged depth with per-level venue attribution. |
| `get_venue_book`       | `const OrderBook* get_venue_book(VenueId id, const Symbol& sym) const` | Get a specific venue's book. Returns nullptr if not found. |
| `is_stale`             | `bool is_stale(const Symbol& sym, std::chrono::microseconds max_age) const` | Check if the data for a symbol is older than `max_age`. |

### Callbacks

| Method              | Signature                                       | Description              |
|---------------------|-------------------------------------------------|--------------------------|
| `set_nbbo_callback` | `void set_nbbo_callback(NBBOCallback cb)`       | Register a callback invoked whenever the NBBO changes. `NBBOCallback = std::function<void(const Symbol&, const NBBO&)>`. |

### Key Types

**NBBO:**

```cpp
struct NBBO {
    Price     best_bid;
    Quantity  best_bid_qty;
    VenueId   best_bid_venue;
    Price     best_ask;
    Quantity  best_ask_qty;
    VenueId   best_ask_venue;
    Timestamp timestamp;

    Price spread() const noexcept;
    Price mid_price() const noexcept;
    bool  valid() const noexcept;
};
```

**AggregatedBook:**

```cpp
struct AggregatedBook {
    Symbol symbol;
    NBBO   nbbo;

    struct AggregatedLevel {
        Price    price;
        Quantity total_quantity;
        std::array<VenueQuantity, 16> venue_breakdown;
        size_t   venue_count;
    };

    std::array<AggregatedLevel, MAX_DEPTH> bids;  // MAX_DEPTH = 20
    std::array<AggregatedLevel, MAX_DEPTH> asks;
    size_t bid_depth;
    size_t ask_depth;
};
```

---

## 8. ExecutionHandler

**Header:** `execution/execution_handler.h`
**Namespace:** `sor::execution`

Manages the order lifecycle after routing. Tracks parent/child relationships,
processes execution reports, propagates fills, and triggers rerouting.

### Order Tracking

| Method              | Signature                                                  | Description              |
|---------------------|------------------------------------------------------------|--------------------------|
| `track_order`       | `void track_order(const Order& parent)`                    | Register a new parent order being tracked. |
| `track_child_order` | `void track_child_order(OrderId parent_id, const Order& child)` | Register a child order under a parent. |

### Execution Report Processing

| Method                | Signature                                              | Description              |
|-----------------------|--------------------------------------------------------|--------------------------|
| `on_execution_report` | `void on_execution_report(const ExecutionReport& report)` | Process an execution report from a venue. Updates child state, propagates fills to parent, checks completion, triggers rerouting. |

### Order Query

| Method              | Signature                                              | Description              |
|---------------------|--------------------------------------------------------|--------------------------|
| `get_order`         | `const Order* get_order(OrderId id) const`             | Look up an order (parent or child) by ID. |
| `get_mutable_order` | `Order* get_mutable_order(OrderId id)`                 | Mutable access to an order by ID. |
| `get_children`      | `std::vector<OrderId> get_children(OrderId parent) const` | Get child order IDs for a parent. |
| `get_parent`        | `OrderId get_parent(OrderId child) const`              | Get the parent order ID for a child. |
| `is_complete`       | `bool is_complete(OrderId parent) const`               | Check if the parent order is fully filled. |

### Callbacks

| Method                     | Signature                                                  | Description              |
|----------------------------|------------------------------------------------------------|--------------------------|
| `set_fill_callback`        | `void set_fill_callback(FillCallback cb)`                  | Called on each fill. `FillCallback = std::function<void(const Order&, const ExecutionReport&)>`. |
| `set_completion_callback`  | `void set_completion_callback(CompletionCallback cb)`      | Called when a parent order is fully filled. `CompletionCallback = std::function<void(const Order&)>`. |
| `set_reroute_callback`     | `void set_reroute_callback(RerouteCallback cb)`            | Called when a child reaches a terminal state with remaining parent quantity. `RerouteCallback = std::function<void(Order&)>`. |

### Statistics

```cpp
struct Stats {
    uint64_t total_fills{0};
    uint64_t total_partial_fills{0};
    uint64_t total_rejects{0};
    uint64_t total_cancels{0};
    uint64_t reroutes{0};
};

Stats get_stats() const;
```

---

## 9. FillManager

**Header:** `execution/fill_manager.h`
**Namespace:** `sor::execution`

Records, indexes, and aggregates fill events.

### Recording

| Method        | Signature                                   | Description              |
|---------------|---------------------------------------------|--------------------------|
| `record_fill` | `void record_fill(const FillRecord& fill)`  | Record a fill event. Indexed by order ID and symbol. |

### Queries

| Method                   | Signature                                                       | Description              |
|--------------------------|-----------------------------------------------------------------|--------------------------|
| `get_fills_for_order`    | `std::vector<FillRecord> get_fills_for_order(OrderId id) const` | All fills for a specific order. |
| `get_fills_for_symbol`   | `std::vector<FillRecord> get_fills_for_symbol(const Symbol& sym) const` | All fills for a symbol. |
| `get_all_fills`          | `std::vector<FillRecord> get_all_fills() const`                 | All recorded fills. |

### Aggregations

| Method                    | Signature                                          | Description              |
|---------------------------|----------------------------------------------------|--------------------------|
| `total_filled_quantity`   | `Quantity total_filled_quantity(OrderId id) const`  | Total filled quantity for an order. |
| `average_fill_price`      | `Price average_fill_price(OrderId id) const`        | Volume-weighted average fill price for an order. |
| `total_fees`              | `double total_fees(OrderId id) const`               | Total fees incurred for an order. |
| `vwap`                    | `Price vwap(const Symbol& sym) const`               | VWAP of all fills for a symbol. |

### Utility

| Method  | Signature        | Description              |
|---------|------------------|--------------------------|
| `clear` | `void clear()`   | Clear all recorded fills. |

**FillRecord struct:**

```cpp
struct FillRecord {
    OrderId   order_id;
    OrderId   exec_id;
    Symbol    symbol;
    Side      side;
    Price     price;
    Quantity  quantity;
    VenueId   venue_id;
    double    fee;
    Timestamp timestamp;
};
```

---

## 10. VenueAdapter (Abstract)

**Header:** `connectors/venue_adapter.h`
**Namespace:** `sor::connectors`

Abstract interface for all venue connections. Concrete implementations
include `SimulatedExchange` and `FixAdapter`.

### Connection Lifecycle

| Method         | Signature                          | Description              |
|----------------|------------------------------------|--------------------------|
| `connect`      | `virtual bool connect() = 0`       | Establish connection. Returns true on success. |
| `disconnect`   | `virtual void disconnect() = 0`    | Gracefully disconnect. |
| `is_connected` | `virtual bool is_connected() const = 0` | Thread-safe connection state check. |

### Order Operations

| Method         | Signature                                          | Description              |
|----------------|----------------------------------------------------|--------------------------|
| `send_order`   | `virtual bool send_order(const Order& order) = 0`  | Submit an order to the venue. Returns true if accepted for transmission (does NOT imply venue acknowledgement). |
| `cancel_order` | `virtual bool cancel_order(const CancelRequest& req) = 0` | Request cancellation of an outstanding order. |

### Venue Metadata

| Method       | Signature                                            | Description              |
|--------------|------------------------------------------------------|--------------------------|
| `venue_id`   | `virtual VenueId venue_id() const = 0`               | Numeric venue identifier. |
| `venue_name` | `virtual const char* venue_name() const = 0`         | Human-readable venue name. |
| `status`     | `virtual VenueStatus status() const = 0`             | Current status: Connected, Disconnected, or Degraded. |
| `avg_latency`| `virtual std::chrono::microseconds avg_latency() const = 0` | Rolling average round-trip latency. |

### Callback Registration

| Method                  | Signature                                          | Description              |
|-------------------------|----------------------------------------------------|--------------------------|
| `set_execution_callback`| `void set_execution_callback(ExecutionCallback cb)` | Register the callback invoked when execution reports arrive from the venue. `ExecutionCallback = std::function<void(const ExecutionReport&)>`. |

### Concrete Implementations

**SimulatedExchange** (`connectors/simulated_exchange.h`):

```cpp
struct Config {
    VenueId     venue_id{1};
    std::string name{"SimExchange"};
    std::chrono::microseconds latency{50};
    std::chrono::microseconds latency_jitter{10};
    double fill_probability{0.95};
    double partial_fill_probability{0.3};
    double reject_probability{0.01};
    double fee_rate{0.001};
};
```

Additional methods:
- `process_matching()` -- process pending orders through the matching engine.
- `set_market_price(Price bid, Price ask)` -- set simulated market prices.
- `get_stats()` -- return matching engine statistics.

**FixAdapter** (`connectors/fix_adapter.h`):

```cpp
struct Config {
    VenueId     venue_id{2};
    std::string name{"FixVenue"};
    std::string sender_comp_id{"SOR_CLIENT"};
    std::string target_comp_id{"EXCHANGE"};
    std::chrono::microseconds simulated_latency{100};
};
```

Additional methods:
- `process_incoming()` -- process queued incoming FIX messages.
- `inject_message(FixMessage msg)` -- inject a message for testing.
- `sent_messages()` -- read-only access to outbound message log.

---

## 11. OrderStateMachine

**Header:** `state/order_state_machine.h`
**Namespace:** `sor::state`

Enforces deterministic, FIX-protocol-aligned state transitions using a
compile-time transition table.

### Static Methods

| Method                  | Signature                                                           | Description              |
|-------------------------|---------------------------------------------------------------------|--------------------------|
| `is_valid_transition`   | `static bool is_valid_transition(OrderState from, OrderEvent event) noexcept` | Check if a transition is defined. |
| `apply` (query)         | `static std::optional<OrderState> apply(OrderState current, OrderEvent event) noexcept` | Return the target state, or `std::nullopt` if invalid. |
| `apply` (mutate)        | `static bool apply(Order& order, OrderEvent event) noexcept`       | Apply event to an order, updating `state` and incrementing `version`. Returns false (order unchanged) on invalid transition. |
| `valid_events`          | `static std::vector<OrderEvent> valid_events(OrderState state)`    | Enumerate all valid events from a given state. |
| `to_string` (state)     | `static const char* to_string(OrderState state) noexcept`          | Human-readable state name. |
| `to_string` (event)     | `static const char* to_string(OrderEvent event) noexcept`          | Human-readable event name. |

### OrderEvent Enum

```cpp
enum class OrderEvent : uint8_t {
    Submit         = 0,
    Acknowledge    = 1,
    PartialFill    = 2,
    Fill           = 3,
    RequestCancel  = 4,
    CancelAck      = 5,
    Reject         = 6,
    Expire         = 7,
    RequestReplace = 8,
    ReplaceAck     = 9,
};
```

---

## 12. ConfigManager

**Header:** `infra/config.h`
**Namespace:** `sor::infra`

Singleton YAML configuration manager with hot-reload support.

### Access

```cpp
static ConfigManager& instance();
```

### Loading

| Method   | Signature                              | Description              |
|----------|----------------------------------------|--------------------------|
| `load`   | `bool load(const std::string& path)`   | Load configuration from a YAML file. Returns true on success. |
| `reload` | `bool reload()`                        | Re-read the file last passed to `load()`. |

### Configuration Access

| Method       | Signature                                    | Description              |
|--------------|----------------------------------------------|--------------------------|
| `get_config` | `SystemConfig get_config() const`            | Thread-safe snapshot of the full config. |
| `get_string` | `std::string get_string(key, default) const` | Flat key-value access (dot notation). |
| `get_int`    | `int get_int(key, default) const`            | Integer config value.    |
| `get_double` | `double get_double(key, default) const`      | Double config value.     |
| `get_bool`   | `bool get_bool(key, default) const`          | Boolean config value.    |

### Hot Reload

| Method              | Signature                            | Description              |
|---------------------|--------------------------------------|--------------------------|
| `check_for_changes` | `void check_for_changes()`           | Poll file mtime; reload if newer. Call from a background thread. |
| `on_change`         | `void on_change(ChangeCallback cb)`  | Register a callback invoked on config change. `ChangeCallback = std::function<void(const SystemConfig&)>`. |

---

## 13. MetricsManager

**Header:** `infra/metrics.h`
**Namespace:** `sor::infra`

Prometheus-style metrics with atomic storage and optional HTTP endpoint.

### Access

```cpp
static MetricsManager& instance();
```

### Lifecycle

| Method     | Signature                      | Description              |
|------------|--------------------------------|--------------------------|
| `init`     | `bool init(int port = 9090)`   | Initialize the metrics system and HTTP endpoint. |
| `shutdown` | `void shutdown()`              | Shut down the HTTP endpoint. |

### Generic Metrics

| Method      | Signature                                     | Description              |
|-------------|-----------------------------------------------|--------------------------|
| `increment` | `void increment(const std::string& name, double value = 1.0)` | Increment a counter. |
| `set_gauge` | `void set_gauge(const std::string& name, double value)` | Set an absolute gauge value. |
| `observe`   | `void observe(const std::string& name, double value)` | Record a histogram observation. |

### Pre-Defined SOR Metrics

| Method                     | Description                          |
|----------------------------|--------------------------------------|
| `record_order_latency(us)` | Record end-to-end order latency.     |
| `record_routing_latency(us)`| Record routing decision latency.    |
| `record_venue_latency(venue, us)` | Record per-venue latency.     |
| `increment_orders_routed()`| Increment routed order counter.      |
| `increment_orders_rejected()`| Increment rejected order counter.  |
| `increment_fills()`        | Increment fill counter.              |
| `set_active_orders(count)` | Set active order gauge.              |
| `set_position(symbol, qty)`| Set position gauge for a symbol.     |

### Scoped Timer (RAII)

```cpp
// Automatic latency measurement:
{
    MetricsManager::ScopedTimer timer("routing_latency_us");
    // ... code being timed ...
}  // timer records elapsed microseconds on destruction

// Or use the convenience macro:
SOR_METRICS_TIMER("routing_latency_us");
```

---

## 14. Tracer

**Header:** `infra/tracing.h`
**Namespace:** `sor::infra`

Per-order lifecycle tracing for post-trade analysis and latency debugging.

### Access

```cpp
static Tracer& instance();
```

### Tracing API

| Method        | Signature                                                              | Description              |
|---------------|------------------------------------------------------------------------|--------------------------|
| `begin_trace` | `void begin_trace(OrderId id)`                                        | Start tracing an order. Records the start timestamp. |
| `trace`       | `void trace(OrderId id, const std::string& stage, const std::string& detail = "")` | Record an intermediate event (e.g., "risk_check", "route", "venue_send", "fill"). |
| `end_trace`   | `void end_trace(OrderId id)`                                          | Mark the trace as complete. |
| `get_trace`   | `std::vector<TraceEvent> get_trace(OrderId id) const`                 | Retrieve the full event list. |
| `dump_trace`  | `std::string dump_trace(OrderId id) const`                            | Human-readable trace string. |
| `is_tracing`  | `bool is_tracing(OrderId id) const`                                   | Check if an order is actively being traced. |
| `gc`          | `void gc(size_t max_traces = 10000)`                                  | Garbage-collect completed traces. |

**TraceEvent struct:**

```cpp
struct TraceEvent {
    OrderId                         order_id;
    std::string                     stage;
    std::string                     detail;
    Timestamp                       timestamp;
    std::chrono::microseconds       latency_from_start{0};
};
```

---

## 15. Core Types Quick Reference

### Price and Quantity

```cpp
using Price    = int64_t;    // Fixed-point, scaled by PRICE_SCALE (1e8)
using Quantity = int64_t;    // Signed (supports short-selling)
using OrderId  = uint64_t;   // Monotonically increasing
using VenueId  = uint16_t;   // Up to 65535 venues

inline constexpr int64_t PRICE_SCALE = 100'000'000LL;

Price to_price(double d) noexcept;     // double -> fixed-point
double to_double(Price p) noexcept;    // fixed-point -> double
```

### Enumerations

```cpp
enum class Side : uint8_t       { Buy, Sell };
enum class OrderType : uint8_t  { Limit, Market, IOC, FOK };
enum class TimeInForce : uint8_t { GTC, IOC, FOK, GTD, DAY };
enum class OrderState : uint8_t  { New, PendingNew, Accepted, PartiallyFilled,
                                   Filled, PendingCancel, Canceled, Rejected,
                                   Expired, PendingReplace };
enum class RoutingStrategy : uint8_t { BestPrice, LiquiditySweep, SmartIOC, VWAP };
enum class VenueStatus : uint8_t     { Connected, Disconnected, Degraded };
```

### Order Struct

```cpp
struct alignas(64) Order {
    OrderId         id;
    OrderId         client_order_id;
    OrderId         parent_order_id;    // non-zero for child orders
    Symbol          symbol;             // FixedString<16>
    Side            side;
    OrderType       type;
    TimeInForce     tif;
    Price           price;
    Quantity        quantity;
    Quantity        filled_quantity;
    Quantity        remaining_quantity;
    Price           avg_fill_price;
    VenueId         target_venue;
    RoutingStrategy strategy;
    OrderState      state;
    ClientId        client_id;          // FixedString<32>
    Timestamp       create_time;
    Timestamp       last_update_time;
    uint32_t        version;            // optimistic concurrency control

    Quantity leaves_qty() const noexcept;
    bool is_terminal() const noexcept;
    bool is_active() const noexcept;
};
```

### ExecutionReport Struct

```cpp
struct alignas(64) ExecutionReport {
    OrderId         order_id;
    OrderId         exec_id;
    OrderState      state;
    Price           last_price;
    Quantity        last_quantity;
    Price           avg_price;
    Quantity        cum_quantity;
    Quantity        leaves_quantity;
    VenueId         venue_id;
    Timestamp       timestamp;
    FixedString<64> text;               // reject reason or info
};
```

---

*See also: [System Documentation](system.md) | [Architecture](architecture.md) | [Developer Guide](developer_guide.md)*
