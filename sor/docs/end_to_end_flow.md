# End-to-End Order Flow

Step-by-step walkthrough of how an order flows through the SOR system, from injection to fill reporting. Code references point to `app/main.cpp` and the modules it wires together.

## Flow Diagram

```
  Order Injection          Market Data           ZMQ Gateway
       │                       │                      │
       │                       ▼                      │
       │              ┌─────────────────┐             │
       │              │ SimulatedFeed   │             │
       │              │ Handler(s)      │             │
       │              └────────┬────────┘             │
       │                       │ on_book_update        │
       │                       ▼                      │
       │              ┌─────────────────┐  NBBO PUB   │
       │              │ MD Aggregator   │────────────▶│
       │              │ (computes NBBO) │             │
       │              └────────┬────────┘             │
       ▼                       │                      │
  ┌─────────┐                  │                      │
  │ Order   │    NBBO for      │                      │
  │ Created │    pricing       │                      │
  └────┬────┘◀─────────────────┘                      │
       │                                              │
       ▼                                              │
  ┌─────────────┐                                     │
  │ State:      │                                     │
  │ New→Pending │                                     │
  └─────┬───────┘                                     │
        │                                             │
        ▼                                             │
  ┌─────────────┐                                     │
  │ Routing     │ route_order(parent)                  │
  │ Engine      │ → RoutingDecision{slices[]}          │
  └─────┬───────┘                                     │
        │                                             │
        ▼                                             │
  ┌─────────────┐  for each slice:                    │
  │ Child Order │                                     │
  │ Creation    │  track_child_order()                 │
  └─────┬───────┘                                     │
        │                                             │
        ▼                                             │
  ┌─────────────┐  rate_limiter.try_acquire()          │
  │ Rate Limit  │                                     │
  │ Check       │                                     │
  └─────┬───────┘                                     │
        │                                             │
        ▼                                             │
  ┌─────────────┐  exchange.send_order(child)          │
  │ Venue Send  │                                     │
  │ (Simulated  │                                     │
  │  Exchange)  │                                     │
  └─────┬───────┘                                     │
        │                                             │
        ▼                                             │
  ┌─────────────┐  exchange.process_matching()         │
  │ Matching    │                                     │
  │ Engine      │                                     │
  └─────┬───────┘                                     │
        │ ExecutionReport callback                    │
        ▼                                             │
  ┌─────────────┐  on_execution_report()               │
  │ Execution   │  → fill aggregation                 │
  │ Handler     │  → parent state update               │
  │             │  → reroute if partial                │
  └─────┬───────┘                                     │
        │                                             │
        ├──▶ fill_callback   ──────────────────▶ FILL PUB
        ├──▶ completion_callback ──────────────▶ COMPLETE PUB
        └──▶ reroute_callback  → pending_reroutes queue
                                    │
                                    ▼
                              (processed next cycle)
```

## Detailed Steps

### 1. Configuration & Startup (`main.cpp:93-165`)

The app loads config from YAML (or uses built-in defaults for simulation), validates it, then initializes:
- **Logger** — spdlog with console + optional file sink
- **Metrics** — Prometheus pull endpoint on configured port
- **Venues** — `SimulatedExchange` per enabled venue, each wrapped with a `RateLimiter`

```
ConfigManager::load(path)
  → parse_yaml()
  → validate_config()        // fail fast on bad config
  → logging/metrics init
  → create VenueConnection[] // exchange + rate_limiter per venue
```

### 2. Market Data Setup (`main.cpp:203-241`)

For each venue, a `SimulatedFeedHandler` generates synthetic order books:

```
SimulatedFeedHandler.generate_tick()
  → random price walk
  → builds bid/ask levels
  → calls book_callback(venue_id, symbol, book)
    → MarketDataAggregator.on_book_update()
      → computes cross-venue NBBO
      → calls nbbo_callback(symbol, nbbo)
        → ZMQ PUB market data (if enabled)
```

10 initial ticks populate books before any orders are routed.

### 3. Order Creation & Routing (`main.cpp:455-557`)

For each order in the simulation batch:

```
1. Refresh market data: feed.generate_tick()
2. Create parent Order with:
   - ID from atomic counter
   - Symbol, side, quantity, price from NBBO
   - Strategy assigned round-robin (BestPrice/LiqSweep/SmartIOC/VWAP)
3. State transition: New → PendingNew
   OrderStateMachine::apply(order, Submit)
4. Route: router.route_order(order) → RoutingDecision
   - Strategy inspects NBBO, venue scores
   - Returns slices: [{venue_id, price, quantity, type, tif}]
5. For each slice:
   a. Create child Order linked to parent (parent_order_id)
   b. exec_handler.track_child_order(parent_id, child)
   c. Rate-limit check: venue.rate_limiter.try_acquire()
   d. venue.exchange.send_order(child)
```

### 4. Matching & Execution (`main.cpp:617-636`)

The main loop runs 5 matching cycles:

```
for cycle in 0..5:
  1. Generate new ticks (market moves)
  2. Update exchange market prices from NBBO
  3. exchange.process_matching() for each venue
     → Fills generate ExecutionReport callbacks
     → exec_handler.on_execution_report(report)
  4. Process pending reroutes (queue-based, not inline)
```

### 5. Execution Report Handling

When a venue fills a child order:

```
SimulatedExchange → execution_callback(ExecutionReport)
  → ExecutionHandler.on_execution_report()
    → Update child order state (PartiallyFilled / Filled)
    → Aggregate fills to parent order
       - parent.filled_quantity += report.last_quantity
       - parent.avg_fill_price = weighted average
    → If child fully filled:
       - If parent fully filled → completion_callback(parent)
       - If parent has remaining → reroute_callback(parent)
    → fill_callback(order, report)
       → ZMQ PUB execution event
```

### 6. Reroute Processing (`main.cpp:560-615`)

Reroutes are **queued** (not inline) to avoid deadlocks:

```
reroute_callback:
  lock(reroute_mutex)
  pending_reroutes.push_back(parent.id)

process_reroutes() [called after each matching cycle]:
  swap(pending_reroutes → to_reroute)
  for each parent_id:
    router.route_order(*parent)  // re-route remaining qty
    create new child orders
    send through rate-limited venue path
```

### 7. ZMQ Transport (`gateway/zmq_transport.cpp`)

Three sockets run when `gateway.api.enabled: true`:

| Socket | Pattern | Purpose |
|--------|---------|---------|
| `tcp://*:5555` | REQ/REP | JSON order submission & status queries |
| `tcp://*:5556` | PUB | NBBO market data updates (multi-part: topic + payload) |
| `tcp://*:5557` | PUB | Execution events: fills and completions |

The REP socket runs in its own thread (`request_loop`). PUB sockets are called from main thread under `pub_mutex_`.

### 8. Statistics & Shutdown (`main.cpp:638-743`)

After simulation completes:
1. Print per-order results (strategy, child count, state, fill qty)
2. Print per-venue statistics (received, filled, partial, rejected, latency)
3. Print execution summary (fills, partials, rejects, reroutes, completions)
4. Print routing summary (routed, rejected, total slices)
5. Print ZMQ transport stats (if enabled)
6. Clean shutdown: stop ZMQ → stop feeds → disconnect venues → shutdown metrics

## Key Design Decisions

- **Queue-based reroute**: Reroute callbacks don't send orders inline. They queue parent IDs for processing after matching, preventing re-entrant deadlocks.
- **Per-venue rate limiting**: Each venue has its own `RateLimiter` wrapping the send path. Rate limits come from config (`max_order_rate`).
- **Fixed-point prices**: All prices use 64-bit fixed-point (8 decimal places) to avoid floating-point drift.
- **Composition over inheritance**: Venues wrap `SimulatedExchange` + `RateLimiter` in a `VenueConnection` struct rather than subclassing.
- **Config-driven**: Every endpoint, rate limit, fee, and parameter comes from YAML config. The app validates at startup and fails fast.
