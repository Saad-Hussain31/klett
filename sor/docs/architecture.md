# Smart Order Router -- Architecture Documentation

## 1. High-Level Architecture

The SOR follows a **pipeline architecture** where an order flows through
discrete stages, each owned by its own module. Communication between stages
uses lock-free queues to decouple producers from consumers and eliminate
mutex contention on the hot path.

```
                         +-----------------+
                         |  Market Data    |
                         |  (Aggregator,   |
                         |   Feed Handler, |
                         |   Replay Engine)|
                         +--------+--------+
                                  |
                                  | NBBO + Depth
                                  v
 +----------+    MPSC    +--------+--------+           +------------------+
 |  Client  | ---------> |    Gateway      | --------> |   Risk Manager   |
 | (FIX /   |   Queue    | (FIX Gateway,   |           | (Rate Limiter,   |
 |  JSON)   |            |  API Gateway)   |           |  Kill Switch,    |
 +----------+            +--------+--------+           |  Position Limits)|
                                  |                    +--------+---------+
                                  |                             |
                                  |                    Pass / Reject
                                  |                             |
                                  v                             v
                         +--------+---------+          +--------+---------+
                         |  Routing Engine  | <--------+  Order State     |
                         |  (BestPrice,     |          |  Machine         |
                         |   LiquiditySweep,|          +------------------+
                         |   SmartIOC, VWAP)|
                         +--------+---------+
                                  |
                                  | RoutingDecision (slices)
                                  v
                         +--------+---------+
                         | Execution Handler|
                         | (Parent/Child    |
                         |  Tracking,       |
                         |  Fill Manager)   |
                         +--------+---------+
                                  |
                         Child orders dispatched
                                  |
                  +---------------+----------------+
                  |               |                |
           +------+----+  +------+-----+  +-------+------+
           | Venue     |  | Venue      |  | Venue        |
           | Connector |  | Connector  |  | Connector    |
           | (Sim      |  | (FIX Mock  |  | (Future:     |
           |  Exchange)|  |  Adapter)  |  |  Live Feed)  |
           +-----------+  +------------+  +--------------+
```

Execution reports flow back from venue connectors through an SPSC queue
to the execution handler, which updates parent order state and triggers
rerouting if child orders are rejected with remaining quantity.

---

## 2. Subsystem Detail

### 2.1 Core (`core/`)

Foundation types shared across every subsystem. All structures in `core/` are
designed for zero-allocation hot paths: no `std::string`, no heap containers,
no virtual dispatch.

| Component          | File(s)              | Purpose                                     |
|--------------------|----------------------|---------------------------------------------|
| **Types**          | `types.h`, `types.cpp` | Fundamental type aliases (`Price`, `Quantity`, `OrderId`, `VenueId`, `Timestamp`), enumerations (`Side`, `OrderType`, `TimeInForce`, `OrderState`, `RoutingStrategy`, `VenueStatus`), `FixedString<N>` template, conversion helpers (`to_price`, `to_double`). |
| **Order**          | `order.h`, `order.cpp` | `Order` (64-byte cache-line aligned), `ExecutionReport`, `CancelRequest` structs. Order carries parent/child ID linkage, routing strategy tag, and an optimistic concurrency `version` counter. |
| **FixedPoint**     | `fixed_point.h`, `fixed_point.cpp` | `FixedPoint<Scale>` template class for exact arithmetic. Uses `__int128` for intermediate multiply/divide to avoid overflow. Pre-defined aliases: `PriceFixed` (scale 1e8, 8 decimal places), `FeeRate` (scale 1e6, 6 decimal places). C++20 spaceship operator for comparison. |
| **MemoryPool**     | `memory_pool.h`, `memory_pool.cpp` | Lock-free Treiber-stack object pool with tagged pointers (ABA prevention). Pre-allocates contiguous storage for `PoolSize` objects (default 4096). `allocate()` and `deallocate()` are lock-free. Cache-line aligned free-list head and available counter to prevent false sharing. |
| **SPSCQueue**      | `spsc_queue.h`       | Lock-free single-producer single-consumer bounded ring buffer. Power-of-two capacity for bitwise modular indexing. Head and tail on separate 64-byte cache lines. Used for the execution report path (single venue thread -> single processing thread). Default capacity: 8192. |
| **MPSCQueue**      | `mpsc_queue.h`       | Lock-free multi-producer single-consumer bounded queue based on Vyukov's design. Per-cell sequence counters coordinate multiple producer threads. Consumer path is wait-free. Used for order ingestion (multiple gateway threads -> single processing thread). Default capacity: 8192. |

**Design decisions:**

- **Fixed-point prices** (`int64_t` scaled by 1e8) avoid IEEE 754 rounding
  errors that are unacceptable in financial arithmetic. The scale of 1e8
  provides sub-cent precision sufficient for equities, FX, and crypto.
- **Cache-line alignment** (`alignas(64)`) on `Order`, queue indices, and pool
  metadata prevents false sharing when structures are accessed from multiple
  threads.
- **`FixedString<N>`** eliminates heap allocation for symbols (16 chars),
  venue names (32 chars), and client IDs (32 chars). `std::hash` is
  specialized so these can be used as keys in `std::unordered_map`.
- **Queue capacities are powers of two** so that index wrapping uses bitwise
  AND instead of modulo division.

### 2.2 Gateway (`gateway/`)

The gateway layer is the entry point for all orders into the SOR. It owns the
main processing loops and all subsystem references.

| Component        | File(s)                    | Purpose                              |
|------------------|----------------------------|--------------------------------------|
| **Gateway**      | `gateway.h`, `gateway.cpp` | Top-level orchestrator. Owns the order MPSC queue, cancel MPSC queue, and execution report SPSC queue. Spawns two threads: order processing loop and execution report processing loop. Provides `submit_order()` and `cancel_order()` as the thread-safe external API. Holds references to all subsystems (market data aggregator, risk manager, routing engine, execution handler, fill manager). Manages venue adapter registration via `add_venue()`. |
| **ApiGateway**   | `api_gateway.h`, `api_gateway.cpp` | JSON API adapter. Provides four endpoints: `handle_new_order()`, `handle_cancel_order()`, `handle_query_order()`, `handle_status()`. Each accepts a JSON string, translates to internal types, invokes the core Gateway, and returns a JSON response. |
| **FixGateway**   | `fix_gateway.h`, `fix_gateway.cpp` | Simplified FIX protocol adapter. Parses inbound FIX messages (35=D NewOrderSingle, 35=F OrderCancelRequest), translates to `Order`/`CancelRequest`, and submits to the core Gateway. Provides a send callback for outbound execution reports formatted as FIX strings. |

**Queue architecture:**

```
  Multiple API/FIX threads                  Single order thread
  ========================                  ===================
       |         |                                 |
       v         v                                 v
  +----+---------+----+                    +-------+-------+
  |   MPSC Order Queue | ================> | order_processing_loop() |
  |   (capacity 8192)  |                   | - risk check            |
  +-------------------+                    | - route                 |
                                           | - dispatch to venues    |
  +-------------------+                    +-------+-------+
  | MPSC Cancel Queue  | ================>         |
  | (capacity 4096)    |                           v
  +-------------------+                    venue connectors
                                                   |
                                                   v
  +-------------------+                    +-------+-------+
  | SPSC Report Queue  | <================ | venue exec reports      |
  | (capacity 8192)    |                   +-------+-------+
  +-------------------+                            |
         |                                         v
         v                                 +-------+-------+
  execution_processing_loop() -----------> | ExecutionHandler       |
  (single thread)                          | FillManager            |
                                           +-----------------------+
```

The order processing loop drains the MPSC order queue and cancel queue on
each iteration, processes each order synchronously (risk check, routing,
child order dispatch), and then yields. The execution processing loop
drains the SPSC report queue and forwards reports to the `ExecutionHandler`.

### 2.3 Risk (`risk/`)

Every order passes through `RiskManager::check_order()` before being routed.
Checks are ordered from cheapest to most expensive to minimize hot-path
latency:

| Check Order | Check                     | Cost        | Implementation           |
|-------------|---------------------------|-------------|--------------------------|
| 1           | Kill switch               | Atomic load | `KillSwitch::is_active()` -- `std::atomic<bool>` |
| 2           | Rate limit                | Atomic CAS  | `RateLimiter::try_acquire()` -- sliding-window counter |
| 3           | Max order quantity        | Arithmetic  | Compare `order.quantity` against `RiskLimits::max_order_quantity` |
| 4           | Max order notional        | Arithmetic  | Compare `price * qty` against `RiskLimits::max_order_notional` |
| 5           | Max position quantity     | Map lookup  | Look up `PositionInfo` by symbol, check net + pending |
| 6           | Max position notional     | Map lookup  | Same position lookup, compute notional |
| 7           | Max open orders           | Map lookup  | Check `PositionInfo::open_order_count` |
| 8           | Max loss                  | Map lookup  | Check `realized_pnl + unrealized_pnl` against limit |

The first check to fail short-circuits the remaining checks and returns a
`RiskCheckResult` enum value identifying the specific failure.

| Component        | File(s)                          | Purpose                    |
|------------------|----------------------------------|----------------------------|
| **RiskManager**  | `risk_manager.h`, `risk_manager.cpp` | Central risk check orchestrator. Maintains global limits, per-symbol limits, and per-symbol position state. Updates positions on fills and order lifecycle events. |
| **RateLimiter**  | `rate_limiter.h`, `rate_limiter.cpp` | Lock-free sliding-window rate limiter. Tracks orders per second using an atomic counter and epoch-second window boundary. |
| **KillSwitch**   | `kill_switch.h`, `kill_switch.cpp`   | Global circuit breaker. `is_active()` is a single atomic load (zero contention). `activate()` triggers registered callbacks synchronously. |

**Position tracking** is updated on:
- `on_fill()` -- adjusts `net_quantity`, `avg_entry_price`, `realized_pnl`.
- `on_order_accepted()` -- increments `open_order_count` and pending quantity.
- `on_order_canceled()` / `on_order_rejected()` -- decrements accordingly.

Limits are configured via `set_global_limits()` and `set_symbol_limits()`. A
zero value for any limit field means "not enforced."

### 2.4 Routing (`routing/`)

The routing engine is the core decision-making component.

| Component              | File(s)                              | Purpose                |
|------------------------|--------------------------------------|------------------------|
| **RoutingEngine**      | `engine.h`, `engine.cpp`             | Orchestrator. Accepts an `Order`, queries the market data aggregator for NBBO and aggregated depth, collects venue scores, delegates to the appropriate strategy, and returns a `RoutingDecision`. Provides callbacks for successful routing and rejection. |
| **RoutingStrategy**    | `strategy.h`                         | Abstract base class. Every strategy implements `route(order, nbbo, book, venues) -> RoutingDecision`. Also defines `RoutingDecision` (vector of `Slice` structs) and `VenueScore` (latency, fill rate, fee rate, fee-adjusted price, availability). |
| **BestPriceStrategy**  | `best_price.h`, `best_price.cpp`     | Routes the entire order to the single venue offering the best fee-adjusted price. Tie-breaking favors lower latency and higher historical fill rate. Uses a composite quality score per venue. |
| **LiquiditySweepStrategy** | `liquidity_sweep.h`, `liquidity_sweep.cpp` | Simultaneously sweeps liquidity across multiple venues, walking the aggregated book from best to worst price. Allocates quantity proportionally to available liquidity at each level. Configurable minimum slice quantity to prevent dust orders. Maximum 10 slices. |
| **SmartIOCStrategy**   | `smart_ioc.h`, `smart_ioc.cpp`       | Optimized for IOC and FOK orders. FOK mode: only routes if a single venue can fill the entire quantity. IOC mode: aggressively sweeps with configurable slippage tolerance (default 5 ticks beyond NBBO). Prefers lower-latency venues for time-critical execution. Maximum 10 slices. |
| **VWAPStrategy**       | `vwap.h`, `vwap.cpp`                 | Time-slices a parent order across a target duration (default 30 minutes, 30 slices). Tracks execution progress and dynamically adjusts pace: accelerates when behind schedule, throttles when ahead. Configurable urgency factor (0.0 = fully passive, 1.0 = fully aggressive) and maximum participation rate (default 25%). Each slice is routed to the best venue via BestPrice logic. |

**RoutingDecision** structure:
```cpp
struct RoutingDecision {
    struct Slice {
        VenueId   venue_id;
        Price     price;
        Quantity  quantity;
        OrderType type{OrderType::Limit};
        TimeInForce tif{TimeInForce::IOC};
    };
    std::vector<Slice> slices;
    bool valid() const noexcept;        // true if slices is non-empty
    Quantity total_quantity() const noexcept;  // sum of all slice quantities
};
```

**VenueScore** structure:
```cpp
struct VenueScore {
    VenueId venue_id;
    double  latency_us;         // avg round-trip in microseconds
    double  fill_rate;          // historical [0, 1]
    double  fee_rate;           // fraction (e.g., 0.001 = 10 bps)
    Price   fee_adjusted_price; // price after fee adjustment
    bool    is_available;
};
```

### 2.5 Market Data (`market_data/`)

Aggregates quotes from multiple venues into a unified view used by the
routing engine.

| Component                | File(s)                              | Purpose              |
|--------------------------|--------------------------------------|----------------------|
| **OrderBook**            | `book.h`, `book.cpp`                 | Per-venue L2 order book. Fixed-size arrays (`MAX_DEPTH = 20` levels per side) for cache-friendly traversal. `BookSide` maintains sorted price levels (bids descending, asks ascending). Provides `quantity_at_or_better()`, `mid_price()`, `spread()`, and crossed-book detection. |
| **MarketDataAggregator** | `aggregator.h`, `aggregator.cpp`     | Maintains `venue_id -> (symbol -> OrderBook)` mapping. Computes NBBO across all registered venues on each update. Builds `AggregatedBook` with merged depth and per-level venue attribution (up to 16 venues per level). Supports staleness detection and NBBO change callbacks. |
| **FeedHandler**          | `feed_handler.h`, `feed_handler.cpp` | Abstract interface for market data sources. `SimulatedFeedHandler` generates deterministic pseudo-random book updates using xorshift64 PRNG. Configurable: initial mid price, tick size, max depth, base quantity, update interval, volatility, RNG seed. |
| **ReplayEngine**         | `replay_engine.h`, `replay_engine.cpp` | Historical market data replay. Loads ticks from CSV or binary files, or generates synthetic random-walk data. Supports speed control (real-time, 2x, as-fast-as-possible), pause/resume, single-step, and reset. |

**NBBO** structure:
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
    bool valid() const noexcept;
};
```

**AggregatedBook** provides merged depth across venues. Each `AggregatedLevel`
contains the total quantity at that price plus a breakdown of up to 16
contributing venues with their individual quantities.

**Market data strategy decision:** The system uses **historical replay plus
simulated feeds** as its baseline. This provides:
- Deterministic, repeatable tests and backtesting.
- No dependency on external API availability, rate limits, or market hours.
- Configurable latency simulation via adjustable update intervals.
- The `FeedHandler` interface is designed for extension: a live adapter can be
  implemented by subclassing `FeedHandler` and connecting to a real market data
  source (e.g., via a binary feed protocol or WebSocket).

### 2.6 Connectors (`connectors/`)

Venue adapters translate between the internal order model and venue-specific
protocols.

| Component            | File(s)                                  | Purpose              |
|----------------------|------------------------------------------|----------------------|
| **VenueAdapter**     | `venue_adapter.h`, `venue_adapter.cpp`   | Abstract interface. Pure virtual methods: `connect()`, `disconnect()`, `is_connected()`, `send_order()`, `cancel_order()`, `venue_id()`, `venue_name()`, `status()`, `avg_latency()`. Provides `set_execution_callback()` and protected `notify_execution()` for dispatching execution reports. |
| **SimulatedExchange**| `simulated_exchange.h`, `simulated_exchange.cpp` | In-process matching engine. Configurable: latency, jitter, fill probability, partial fill probability, reject probability, fee rate. Supports `process_matching()` for deterministic step-through. Maintains active orders, pending queue, and simulated market prices. Uses `std::mt19937` for stochastic behavior. |
| **FixAdapter**       | `fix_adapter.h`, `fix_adapter.cpp`       | Mock FIX 4.4 adapter. Translates `Order` to `FixMessage` (tag-value pairs) and back. Supports NewOrderSingle (35=D), OrderCancelRequest (35=F), ExecutionReport (35=8), Heartbeat, Logon, Logout message types. In-process message queuing for testing (no actual network I/O). |

The `SimulatedExchange` is the primary connector for development, testing, and
backtesting. Its matching engine:
- Accepts orders into a pending queue.
- Matches against simulated market prices on each `process_matching()` call.
- Generates fills (full or partial), rejects, and cancel acknowledgements.
- Simulates latency with configurable base latency and random jitter.
- Tracks statistics: orders received, filled, partially filled, rejected,
  canceled.

### 2.7 Execution (`execution/`)

Manages the order lifecycle after routing decisions are dispatched to venues.

| Component            | File(s)                                    | Purpose            |
|----------------------|--------------------------------------------|--------------------|
| **ExecutionHandler** | `execution_handler.h`, `execution_handler.cpp` | Tracks parent/child order relationships via bidirectional maps (`parent_to_children_`, `child_to_parent_`). Processes execution reports: updates child order state, propagates fills to parent, checks completion, triggers rerouting when a child is rejected or canceled with remaining quantity. Provides callbacks: `FillCallback`, `CompletionCallback`, `RerouteCallback`. |
| **FillManager**      | `fill_manager.h`, `fill_manager.cpp`       | Records and indexes `FillRecord` events. Maintains indices by order ID and by symbol for efficient queries. Provides aggregation functions: `total_filled_quantity()`, `average_fill_price()`, `total_fees()`, and cross-order `vwap()` per symbol. |

**Parent/child order flow:**

1. Gateway assigns a parent order ID and submits to routing.
2. Routing engine produces `RoutingDecision` with N slices.
3. Gateway creates N child orders, each with `parent_order_id` set.
4. `ExecutionHandler::track_order()` registers the parent.
5. `ExecutionHandler::track_child_order()` registers each child under the parent.
6. Venue connectors send child orders and receive execution reports.
7. `ExecutionHandler::on_execution_report()` processes each report:
   - Updates child order state via the state machine.
   - On fill: propagates to parent (`filled_quantity`, `avg_fill_price`).
   - On completion/rejection: checks if parent has remaining unfilled quantity
     and triggers `RerouteCallback` if rerouting is needed.
8. When parent is fully filled, `CompletionCallback` is invoked.

### 2.8 State (`state/`)

| Component             | File(s)                                        | Purpose          |
|-----------------------|------------------------------------------------|------------------|
| **OrderStateMachine** | `order_state_machine.h`, `order_state_machine.cpp` | Enforces deterministic, FIX-protocol-aligned state transitions using a **compile-time transition table**. |

The transition table is a 10x10 `constexpr` array (10 states x 10 events),
built at compile time by `build_transition_table()`. Invalid transitions are
initialized to a sentinel value (255).

**States** (10):
```
New, PendingNew, Accepted, PartiallyFilled, Filled,
PendingCancel, Canceled, Rejected, Expired, PendingReplace
```

**Events** (10):
```
Submit, Acknowledge, PartialFill, Fill, RequestCancel,
CancelAck, Reject, Expire, RequestReplace, ReplaceAck
```

**Key transitions:**

| From            | Event          | To               | Notes                     |
|-----------------|----------------|------------------|---------------------------|
| New             | Submit         | PendingNew       | Order enters the system   |
| PendingNew      | Acknowledge    | Accepted         | Venue accepts             |
| PendingNew      | Reject         | Rejected         | Venue rejects             |
| Accepted        | PartialFill    | PartiallyFilled  | First partial fill        |
| Accepted        | Fill           | Filled           | Fully filled in one shot  |
| Accepted        | RequestCancel  | PendingCancel    | Cancel request sent       |
| Accepted        | Expire         | Expired          | TIF expired               |
| Accepted        | RequestReplace | PendingReplace   | Replace/amend request     |
| PartiallyFilled | PartialFill    | PartiallyFilled  | Additional partial fill   |
| PartiallyFilled | Fill           | Filled           | Final fill                |
| PartiallyFilled | RequestCancel  | PendingCancel    | Cancel remaining          |
| PendingCancel   | CancelAck      | Canceled         | Cancel confirmed          |
| PendingCancel   | Fill           | Filled           | Filled before cancel arrived |
| PendingCancel   | PartialFill    | PendingCancel    | Partial fill in-flight    |
| PendingCancel   | Reject         | Accepted         | Cancel rejected, still live |
| PendingReplace  | ReplaceAck     | Accepted         | Replace confirmed         |
| PendingReplace  | Reject         | Accepted         | Replace rejected          |
| PendingReplace  | Fill           | Filled           | Filled before replace arrived |

The public API:
- `OrderStateMachine::apply(order, event)` -- applies the event, updates
  `order.state` and increments `order.version` on success. Returns false
  (order unchanged) on invalid transition.
- `OrderStateMachine::is_valid_transition(from, event)` -- pure query.
- `OrderStateMachine::valid_events(state)` -- enumerate allowed events.

### 2.9 Infrastructure (`infra/`)

Cross-cutting concerns supporting the entire system.

| Component          | File(s)                      | Purpose                          |
|--------------------|------------------------------|----------------------------------|
| **Logger**         | `logging.h`, `logging.cpp`   | Singleton wrapper around spdlog. Console + optional rotating file sink. Six log levels: Trace, Debug, Info, Warn, Error, Critical. Convenience macros `SOR_LOG_TRACE` through `SOR_LOG_CRITICAL` that short-circuit on a null logger (zero cost when uninitialized). |
| **ConfigManager**  | `config.h`, `config.cpp`     | Singleton YAML configuration manager. Loads `SystemConfig` from YAML (venues, strategy tuning, risk limits, logging, metrics). Supports hot reload via `check_for_changes()` (poll-based mtime comparison). Thread-safe snapshots via `get_config()`. Change callbacks. Flat key-value access with dot-notation (e.g., `"risk.max_loss"`). |
| **MetricsManager** | `metrics.h`, `metrics.cpp`   | Prometheus-style metrics. Counters, gauges, and histograms stored as atomic doubles. Optional prometheus-cpp integration for HTTP `/metrics` endpoint (compile-time optional via opaque `Impl` pointer). Pre-defined SOR metrics: `record_order_latency()`, `record_routing_latency()`, `record_venue_latency()`, `increment_orders_routed()`, etc. RAII `ScopedTimer` for automatic latency measurement. |
| **Tracer**         | `tracing.h`, `tracing.cpp`   | Per-order lifecycle tracing. `begin_trace()` / `trace()` / `end_trace()` record `TraceEvent` entries with stage name, detail string, and microsecond latency from order start. `dump_trace()` produces human-readable output. `gc()` garbage-collects completed traces (keeps at most 10,000 by default). |

**Configuration structure** (`SystemConfig`):
```yaml
log_level: "info"
log_file: "/var/log/sor/sor.log"
enable_metrics: true
metrics_port: 9090
data_dir: "/data/market_data"

venues:
  - venue_id: 1
    name: "SimExchange"
    type: "simulated"
    endpoint: ""
    fee_rate: 0.001
    enabled: true
    max_orders_per_second: 100

strategy:
  default_strategy: "BestPrice"
  vwap_num_slices: 10
  vwap_duration_seconds: 300
  vwap_urgency: 0.5
  sweep_min_slice: 10
  ioc_slippage_ticks: 2

risk:
  max_order_quantity: 10000
  max_order_notional: 0
  max_position_notional: 0
  max_position_quantity: 0
  max_orders_per_second: 100
  max_open_orders: 1000
  max_loss: 0
```

---

## 3. Data Flow (End-to-End)

```
 1. Client sends order via FIX (35=D) or JSON API.

 2. FixGateway / ApiGateway parses the message, creates an Order struct,
    and pushes it onto the MPSC order queue.

 3. Gateway::order_processing_loop() dequeues the Order.

 4. Gateway assigns a monotonic OrderId via atomic increment.

 5. RiskManager::check_order() runs the pre-trade risk check cascade:
    kill switch -> rate limit -> order limits -> position limits.
    If any check fails: reject the order (log, metric, notify client).

 6. MarketDataAggregator::get_nbbo() and get_aggregated_book() provide
    the current market state for the order's symbol.

 7. RoutingEngine::route_order() selects the registered strategy matching
    order.strategy, invokes strategy->route(order, nbbo, book, venues),
    and returns a RoutingDecision with one or more slices.

 8. For each slice in the RoutingDecision:
    a. Create a child Order with parent_order_id set.
    b. Apply OrderStateMachine::apply(child, Submit) -> PendingNew.
    c. ExecutionHandler::track_child_order(parent_id, child).
    d. Look up the target venue adapter by venue_id.
    e. Call venue_adapter->send_order(child).

 9. Venue connector transmits the order. On acknowledgement/fill/reject,
    the connector creates an ExecutionReport and calls notify_execution(),
    which pushes it onto the SPSC report queue.

10. Gateway::execution_processing_loop() dequeues the ExecutionReport.

11. ExecutionHandler::on_execution_report() processes the report:
    a. Look up the child order by report.order_id.
    b. Apply the appropriate state machine event (Acknowledge, PartialFill,
       Fill, Reject, CancelAck).
    c. If fill: update child filled_quantity, compute new avg_fill_price.
       Propagate fill to parent order. Record fill via FillManager.
       Notify risk manager via on_fill().
    d. If child reaches terminal state with parent still unfilled:
       invoke RerouteCallback to send remaining quantity elsewhere.
    e. If parent is fully filled: invoke CompletionCallback.
    f. Record trace event via Tracer::trace().

12. Metrics are updated at each stage: order_latency, routing_latency,
    venue_latency, fill counts, rejection counts, active order gauge.
```

---

## 4. Threading Model

The system uses a small number of dedicated threads connected by lock-free
queues. No mutexes appear on the order-processing hot path.

```
+------------------+     +------------------+     +------------------+
| Order Processing |     | Exec Report      |     | Market Data      |
| Thread           |     | Processing Thread|     | Thread(s)        |
|                  |     |                  |     |                  |
| Drains MPSC      |     | Drains SPSC      |     | FeedHandler or   |
| order queue &    |     | report queue.    |     | ReplayEngine     |
| cancel queue.    |     | Updates state,   |     | generates ticks, |
| Risk check,      |     | propagates fills,|     | updates venue    |
| route, dispatch. |     | triggers reroute.|     | books via        |
|                  |     |                  |     | Aggregator.      |
+--------+---------+     +--------+---------+     +--------+---------+
         |                        ^                        |
         | send_order()           | notify_execution()     | on_book_update()
         v                        |                        v
+--------+---------+     +--------+---------+     +--------+---------+
| Venue Adapter    |     | Venue Adapter    |     | MarketData       |
| Thread (Sim)     |---->| Thread (FIX)     |     | Aggregator       |
+------------------+     +------------------+     +------------------+
```

**Key invariants:**
- The order processing thread is the **only writer** to parent order state
  during routing.
- The exec report processing thread is the **only writer** to order state
  during fill processing.
- These two threads communicate through the SPSC report queue (the exec
  thread is the producer, the order thread consumes reports via the exec
  processing loop -- both loops run on the two dedicated threads spawned
  by `Gateway`).
- Market data aggregator updates and NBBO recomputation happen on the
  market data thread. The routing engine reads NBBO snapshots (the aggregator
  returns copies, so no cross-thread aliasing).

---

## 5. Design Decisions

### 5.1 Fixed-Point Prices

Prices are stored as `int64_t` scaled by 10^8 (100,000,000). This provides
8 decimal places of precision, sufficient for:
- Equities (typically 2-4 decimal places)
- FX (5-6 decimal places)
- Crypto (up to 8 decimal places)

Advantages over `double`:
- Exact comparison (no epsilon-based equality).
- Deterministic arithmetic (no rounding mode dependencies).
- Identical results across compilers and platforms.
- Natural for fee-adjusted price calculation.

The `FixedPoint<Scale>` template uses `__int128` for intermediate products
to avoid overflow when multiplying two scaled values.

### 5.2 Cache-Line Alignment

The `Order` struct is `alignas(64)` (one cache line on x86-64). This ensures:
- No false sharing when orders are stored in contiguous arrays.
- Prefetcher-friendly memory layout for sequential order processing.
- Queue head/tail indices on separate cache lines prevent producer-consumer
  false sharing.

### 5.3 No Heap on the Hot Path

The order processing hot path (risk check through venue dispatch) performs
zero heap allocations:
- `Order` and `ExecutionReport` are value types passed through queues.
- `FixedString<N>` stores symbols, venue names, and client IDs inline.
- `MemoryPool` pre-allocates object storage.
- `RoutingDecision::slices` is the one exception (uses `std::vector`), but
  this vector is typically small (1-10 elements) and could be replaced with
  a fixed-capacity `std::array` + size if needed.

### 5.4 Compile-Time State Machine

The order state transition table is built at compile time via `constexpr`
functions. Benefits:
- Zero runtime initialization cost.
- The table is in read-only memory (`.rodata` section).
- `apply()` is a single array lookup plus comparison -- O(1) with no branches
  beyond the validity check.
- The compiler can verify the table at compile time.

### 5.5 Market Data Strategy

The system defaults to **historical replay plus simulated feeds** rather than
requiring live market data connections. This decision was made because:
- It enables deterministic, repeatable testing and backtesting.
- It removes dependency on external APIs, rate limits, and market hours.
- The `FeedHandler` abstract interface provides a clean extension point for
  adding live feeds without modifying any other subsystem.

---

## 6. Utilities (`utils/`)

| Component      | File(s)                            | Purpose                 |
|----------------|------------------------------------|-------------------------|
| **StringUtils**| `string_utils.h`, `string_utils.cpp` | String formatting and conversion helpers. |
| **TimeUtils**  | `time_utils.h`, `time_utils.cpp`     | Timestamp formatting, duration conversion, wall-clock helpers. |

---

## 7. Test Infrastructure (`tests/`)

Tests use the **Catch2** framework, fetched via CMake FetchContent.

| Suite           | Directory              | Coverage                        |
|-----------------|------------------------|---------------------------------|
| **Unit tests**  | `tests/unit/`          | `test_fixed_point.cpp` -- arithmetic correctness and edge cases. `test_memory_pool.cpp` -- allocation, deallocation, exhaustion, thread safety. `test_market_data.cpp` -- book updates, NBBO computation, aggregation. `test_order_state_machine.cpp` -- all valid transitions, invalid transition rejection. `test_risk_manager.cpp` -- each risk check in isolation, kill switch, rate limiter. `test_routing_strategies.cpp` -- each strategy with known inputs and expected outputs. |
| **Integration** | `tests/integration/`   | `test_order_lifecycle.cpp` -- full order flow from submission through fill. `test_partial_fills.cpp` -- multi-slice partial fill aggregation. `test_venue_failover.cpp` -- child rejection and rerouting. |

---

## 8. Python Tooling (`python/`)

| Package           | Directory                | Purpose                       |
|-------------------|--------------------------|-------------------------------|
| **simulation**    | `python/simulation/`     | `MarketSimulator` -- multi-venue order book simulation. `MockVenue` -- single-venue simulator. `OrderBook` -- Python order book implementation. |
| **backtesting**   | `python/backtesting/`    | `ReplayEngine` -- Python market data replay. `ScenarioRunner` -- parameterized test scenarios. `StrategyEvaluator` -- execution quality analysis. |
| **tools**         | `python/tools/`          | `OrderGenerator` -- synthetic order generation. `FixMessageBuilder` -- FIX message construction. `LatencyAnalyzer` -- trace file analysis and reporting. |

---

*See also: [System Documentation](system.md) | [Developer Guide](developer_guide.md) | [API Reference](api_reference.md)*
