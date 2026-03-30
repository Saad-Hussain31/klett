# Smart Order Router — Diagrams

## 1. System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        SOR SYSTEM                               │
│                                                                 │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────────────────┐  │
│  │FIX Client│  │API Client│  │       Market Data Feeds       │  │
│  └────┬─────┘  └────┬─────┘  └──────────────┬───────────────┘  │
│       │              │                        │                  │
│  ┌────▼─────┐  ┌────▼─────┐  ┌──────────────▼───────────────┐  │
│  │FIX       │  │API       │  │ Market Data Aggregator        │  │
│  │Gateway   │  │Gateway   │  │ (NBBO + L2 Book per venue)   │  │
│  └────┬─────┘  └────┬─────┘  └──────────────┬───────────────┘  │
│       │              │                        │                  │
│       └──────┬───────┘                        │                  │
│              │                                │                  │
│  ┌───────────▼────────────────────────────────┤                  │
│  │           GATEWAY (Order Processing)       │                  │
│  │  ┌─────────────┐  ┌────────────────────┐  │                  │
│  │  │ MPSC Queue  │  │ Risk Manager       │  │                  │
│  │  │ (orders in) │──▶ • Pre-trade checks │  │                  │
│  │  └─────────────┘  │ • Rate limiter     │  │                  │
│  │                    │ • Kill switch      │  │                  │
│  │                    └────────┬───────────┘  │                  │
│  │                             │              │                  │
│  │                    ┌────────▼───────────┐  │                  │
│  │                    │ Routing Engine     │◀─┘                  │
│  │                    │ • BestPrice        │                     │
│  │                    │ • LiquiditySweep   │                     │
│  │                    │ • SmartIOC         │                     │
│  │                    │ • VWAP             │                     │
│  │                    └────────┬───────────┘                     │
│  │                             │ (RoutingDecision: slices)       │
│  │                    ┌────────▼───────────┐                     │
│  │                    │ Execution Handler  │                     │
│  │                    │ (parent/child      │                     │
│  │                    │  order tracking)   │                     │
│  │                    └────────┬───────────┘                     │
│  └─────────────────────────────┤                                 │
│                                │                                 │
│       ┌────────────────────────┼─────────────────┐               │
│       │                        │                 │               │
│  ┌────▼──────┐  ┌─────────────▼──┐  ┌──────────▼────┐          │
│  │Simulated  │  │  FIX Adapter   │  │  (Future)     │          │
│  │Exchange   │  │  (Mock FIX)    │  │  WebSocket    │          │
│  │(matching) │  │                │  │  Adapter      │          │
│  └────┬──────┘  └────────┬───────┘  └───────────────┘          │
│       │                  │                                      │
│       └──────┬───────────┘                                      │
│              │ (ExecutionReports)                                │
│  ┌───────────▼───────────┐                                      │
│  │ SPSC Queue            │                                      │
│  │ (reports back)        │                                      │
│  └───────────┬───────────┘                                      │
│              │                                                   │
│  ┌───────────▼───────────┐                                      │
│  │ Fill Manager          │                                      │
│  │ (aggregate, VWAP,     │                                      │
│  │  fees, PnL)           │                                      │
│  └───────────────────────┘                                      │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │ Infrastructure: spdlog | Prometheus | YAML Config | Tracer│  │
│  └──────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

## 2. Component Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        LIBRARIES                             │
│  ┌─────────┐ ┌────────┐ ┌───────┐ ┌────────┐ ┌──────────┐ │
│  │sor_core │ │sor_risk│ │sor_   │ │sor_    │ │sor_      │ │
│  │         │ │        │ │state  │ │market_ │ │routing   │ │
│  │• types  │ │• risk  │ │       │ │data    │ │          │ │
│  │• order  │ │  mgr   │ │• state│ │        │ │• engine  │ │
│  │• memory │ │• rate  │ │  mach.│ │• book  │ │• best_px │ │
│  │  pool   │ │  limit │ │       │ │• aggr. │ │• sweep   │ │
│  │• spsc_q │ │• kill  │ │       │ │• feed  │ │• ioc     │ │
│  │• mpsc_q │ │  switch│ │       │ │• replay│ │• vwap    │ │
│  │• fixed  │ │        │ │       │ │        │ │          │ │
│  │  point  │ │        │ │       │ │        │ │          │ │
│  └────┬────┘ └───┬────┘ └───┬───┘ └───┬────┘ └────┬─────┘ │
│       │          │          │          │           │        │
│  ┌────▼──────────▼──────────▼──────────▼───────────▼─────┐  │
│  │                  DEPENDENCY GRAPH                      │  │
│  │                                                        │  │
│  │  sor_core ◀── sor_risk                                │  │
│  │  sor_core ◀── sor_state                               │  │
│  │  sor_core ◀── sor_market_data                         │  │
│  │  sor_core + sor_market_data + sor_risk ◀── sor_routing│  │
│  │  sor_core + sor_state ◀── sor_connectors              │  │
│  │  sor_core + sor_state ◀── sor_execution               │  │
│  │  ALL ◀── sor_gateway                                  │  │
│  │  sor_core ◀── sor_infra                               │  │
│  │  sor_core ◀── sor_utils                               │  │
│  └────────────────────────────────────────────────────────┘  │
│                                                              │
│  ┌──────────────┐ ┌────────────┐ ┌────────────────────────┐  │
│  │sor_connectors│ │sor_        │ │sor_gateway             │  │
│  │              │ │execution   │ │                        │  │
│  │• venue_adapt.│ │            │ │• gateway (main)        │  │
│  │• simulated  │ │• exec_     │ │• fix_gateway           │  │
│  │  exchange   │ │  handler   │ │• api_gateway (JSON)    │  │
│  │• fix_adapter│ │• fill_mgr  │ │                        │  │
│  └─────────────┘ └────────────┘ └────────────────────────┘  │
│                                                              │
│  ┌──────────────────────┐ ┌──────────────────────────────┐   │
│  │sor_infra             │ │sor_utils                     │   │
│  │• logging (spdlog)    │ │• time_utils                  │   │
│  │• config (yaml-cpp)   │ │• string_utils                │   │
│  │• metrics (prometheus)│ │                              │   │
│  │• tracing             │ │                              │   │
│  └──────────────────────┘ └──────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

## 3. Order Lifecycle — Sequence Diagram

```
 Client          Gateway        RiskMgr      RoutingEngine    Venue(s)      ExecHandler
   │                │               │              │              │              │
   │  submit_order  │               │              │              │              │
   ├───────────────▶│               │              │              │              │
   │                │               │              │              │              │
   │                │ push to MPSC  │              │              │              │
   │                │──────────┐    │              │              │              │
   │                │          │    │              │              │              │
   │                │◀─────────┘    │              │              │              │
   │                │               │              │              │              │
   │                │ check_order() │              │              │              │
   │                ├──────────────▶│              │              │              │
   │                │               │              │              │              │
   │                │  Passed ✓     │              │              │              │
   │                │◀──────────────┤              │              │              │
   │                │               │              │              │              │
   │                │ route_order()                │              │              │
   │                ├─────────────────────────────▶│              │              │
   │                │               │              │              │              │
   │                │  RoutingDecision (slices)    │              │              │
   │                │◀─────────────────────────────┤              │              │
   │                │               │              │              │              │
   │                │ track orders                 │              │              │
   │                ├──────────────────────────────────────────────────────────▶│
   │                │               │              │              │              │
   │                │ send_order (per slice)       │              │              │
   │                ├─────────────────────────────────────────┬──▶│              │
   │                │               │              │          │   │              │
   │                │               │              │          │   │              │
   │                │  ExecutionReport (Accepted)  │          │   │              │
   │                │◀────────────────────────────────────────┘───┤              │
   │                │               │              │              │              │
   │                │ push to SPSC                 │              │              │
   │                │──────────┐    │              │              │              │
   │                │◀─────────┘    │              │              │              │
   │                │               │              │              │              │
   │                │  ExecutionReport (Fill)       │              │              │
   │                │◀─────────────────────────────────────────────┤              │
   │                │               │              │              │              │
   │                │ on_execution_report()         │              │              │
   │                ├──────────────────────────────────────────────────────────▶│
   │                │               │              │              │              │
   │                │               │              │     update parent, fills    │
   │                │               │              │              │              │
   │  report/ack    │               │              │              │              │
   │◀───────────────┤               │              │              │              │
   │                │               │              │              │              │
```

## 4. Order State Machine

```
                         ┌───────────────────────────────────────────┐
                         │          ORDER STATE MACHINE              │
                         └───────────────────────────────────────────┘

                                    ┌─────┐
                                    │ NEW │
                                    └──┬──┘
                                       │ Submit
                                       ▼
                                 ┌───────────┐
                          ┌──────│PENDING_NEW │──────┐
                          │      └───────────┘      │
                          │ Acknowledge              │ Reject
                          ▼                          ▼
                     ┌──────────┐             ┌──────────┐
            ┌───────▶│ ACCEPTED │             │ REJECTED │ (terminal)
            │        └────┬─────┘             └──────────┘
            │             │
            │    ┌────────┼──────────┬──────────────┐
            │    │        │          │              │
            │    │PartFill│ Fill     │ ReqCancel    │ ReqReplace
            │    ▼        ▼          ▼              ▼
            │ ┌────────┐ ┌──────┐ ┌──────────────┐ ┌───────────────┐
            │ │PARTIAL │ │FILLED│ │PENDING_CANCEL│ │PENDING_REPLACE│
            │ │FILLED  │ │      │ │              │ │               │
            │ └───┬────┘ └──────┘ └──────┬───────┘ └───────┬───────┘
            │     │       (term.)        │                  │
            │     │                      │                  │
            │     ├─PartFill──▶(self)    ├─CancelAck───┐   ├─ReplaceAck──┐
            │     ├─Fill──────▶FILLED    ├─Fill────────▶F   ├─Reject─────▶A
            │     ├─ReqCancel─▶PEND_CXL  ├─PartFill───▶(s)  ├─Fill──────▶F
            │     └─Expire────▶EXPIRED   └─Reject─────▶Acc  │
ReplaceAck  │                                               │
    ────────┘                  ┌─────────┐                   │
                               │CANCELED │◀──────────────────┘
                               └─────────┘
                                (terminal)
                               ┌─────────┐
                               │ EXPIRED │
                               └─────────┘
                                (terminal)

  Legend:  (term.) = terminal state — no further transitions
           (self)  = self-loop — stays in same state
           A = Accepted, F = Filled, (s) = stays in PendingCancel
```

## 5. Threading / Queue Architecture

```
                     Thread 1                    Thread 2
               (Order Processing)          (Execution Processing)
              ┌──────────────────┐        ┌──────────────────────┐
              │                  │        │                      │
 API/FIX ───▶│  drain MPSC      │        │  drain SPSC          │
 clients     │  order_queue_    │        │  report_queue_       │
              │       │          │        │       │              │
              │       ▼          │        │       ▼              │
              │  risk check      │        │  ExecutionHandler    │
              │       │          │        │  .on_exec_report()   │
              │       ▼          │        │       │              │
              │  route_order()   │        │       ▼              │
              │       │          │        │  update parent       │
              │       ▼          │        │  record fill         │
              │  create children │        │  check completion    │
              │       │          │        │  trigger reroute?    │
              │       ▼          │        │                      │
              │  send to venue   │        │                      │
              │       │          │        └──────────────────────┘
              └───────┼──────────┘
                      │                          ▲
                      ▼                          │
              Venue Adapters ─── exec callback ──┘
              (SimExchange,      pushes to
               FixAdapter)       report_queue_

  Queue Types:
   • order_queue_  : MPSCQueue<Order, 8192>     (many clients → 1 processor)
   • cancel_queue_ : MPSCQueue<CancelReq, 4096> (many clients → 1 processor)
   • report_queue_ : SPSCQueue<ExecReport, 8192> (1 callback → 1 processor)
```

## 6. Class Diagram (Core Types)

```
┌────────────────────────────────┐
│          Order                 │
│ (alignas(64))                  │
├────────────────────────────────┤
│ + id: OrderId (uint64_t)       │
│ + client_order_id: OrderId     │
│ + parent_order_id: OrderId     │
│ + symbol: Symbol (FixedStr<16>)│
│ + side: Side                   │
│ + type: OrderType              │
│ + tif: TimeInForce             │
│ + price: Price (int64_t)       │
│ + quantity: Quantity            │
│ + filled_quantity: Quantity     │
│ + remaining_quantity: Quantity  │
│ + avg_fill_price: Price        │
│ + target_venue: VenueId        │
│ + strategy: RoutingStrategy    │
│ + state: OrderState            │
│ + client_id: ClientId          │
│ + create_time: Timestamp       │
│ + last_update_time: Timestamp  │
│ + version: uint32_t            │
├────────────────────────────────┤
│ + leaves_qty(): Quantity       │
│ + is_terminal(): bool          │
│ + is_active(): bool            │
└────────────────────────────────┘

┌────────────────────────────────┐    ┌─────────────────────────────┐
│     ExecutionReport            │    │      CancelRequest          │
│ (alignas(64))                  │    │ (alignas(64))               │
├────────────────────────────────┤    ├─────────────────────────────┤
│ + order_id: OrderId            │    │ + order_id: OrderId         │
│ + exec_id: OrderId             │    │ + client_order_id: OrderId  │
│ + state: OrderState            │    │ + symbol: Symbol            │
│ + last_price: Price            │    │ + side: Side                │
│ + last_quantity: Quantity       │    │ + timestamp: Timestamp      │
│ + avg_price: Price             │    └─────────────────────────────┘
│ + cum_quantity: Quantity        │
│ + leaves_quantity: Quantity     │
│ + venue_id: VenueId            │
│ + timestamp: Timestamp         │
│ + text: FixedString<64>        │
└────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│                    RoutingStrategy (abstract)                     │
├──────────────────────────────────────────────────────────────────┤
│ + route(order, nbbo, book, venues) -> RoutingDecision = 0        │
│ + name() -> const char* = 0                                      │
│ + type() -> RoutingStrategy = 0                                  │
└───────┬──────────┬──────────────┬────────────────┬───────────────┘
        │          │              │                │
   ┌────▼───┐ ┌───▼──────┐ ┌────▼──────┐ ┌──────▼────┐
   │BestPx  │ │LiqSweep  │ │SmartIOC   │ │VWAP       │
   └────────┘ └──────────┘ └───────────┘ └───────────┘

┌──────────────────────────────────────────────────────────────────┐
│                    VenueAdapter (abstract)                        │
├──────────────────────────────────────────────────────────────────┤
│ + connect() -> bool = 0                                          │
│ + disconnect() = 0                                               │
│ + send_order(order) -> bool = 0                                  │
│ + cancel_order(request) -> bool = 0                              │
│ + venue_id() -> VenueId = 0                                     │
│ + venue_name() -> const char* = 0                                │
│ + status() -> VenueStatus = 0                                    │
│ + avg_latency() -> microseconds = 0                              │
└───────────┬──────────────────────────────┬───────────────────────┘
            │                              │
   ┌────────▼──────────┐      ┌───────────▼────────────┐
   │SimulatedExchange   │      │FixAdapter              │
   │(matching engine)   │      │(FIX 4.2 mock)          │
   └───────────────────┘      └────────────────────────┘
```
