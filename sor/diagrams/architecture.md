# System Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        SMART ORDER ROUTER                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────┐   ┌──────────┐                                       │
│  │FIX Client│   │API Client│                                       │
│  └────┬─────┘   └────┬─────┘                                       │
│       │              │                                              │
│       ▼              ▼                                              │
│  ┌──────────────────────────┐                                       │
│  │       GATEWAY LAYER      │  (FIX Parser / JSON-ZMQ)              │
│  └────────────┬─────────────┘                                       │
│               │ Order                                               │
│               ▼                                                     │
│  ┌──────────────────────────┐                                       │
│  │     RISK MANAGER         │  Position limits, notional, rate      │
│  │  ┌─────────┐ ┌────────┐  │  limiter, kill switch                 │
│  │  │Rate Lim │ │Kill Sw │  │                                       │
│  │  └─────────┘ └────────┘  │                                       │
│  └────────────┬─────────────┘                                       │
│               │ Approved Order                                      │
│               ▼                                                     │
│  ┌──────────────────────────┐   ┌──────────────────────────┐        │
│  │    ROUTING ENGINE        │◄──│    MARKET DATA            │        │
│  │                          │   │    AGGREGATOR             │        │
│  │  ┌──────────────────┐    │   │  ┌────────┐ ┌────────┐   │        │
│  │  │ Best Price       │    │   │  │ NBBO   │ │L2 Book │   │        │
│  │  │ Liquidity Sweep  │    │   │  └────────┘ └────────┘   │        │
│  │  │ Smart IOC        │    │   │  ┌────────────────────┐   │        │
│  │  │ VWAP             │    │   │  │ Feed Handler       │   │        │
│  │  └──────────────────┘    │   │  │ Replay Engine      │   │        │
│  └──┬──────┬──────┬─────────┘   └──────────────────────────┘        │
│     │      │      │ Child Orders                                    │
│     ▼      ▼      ▼                                                 │
│  ┌──────┐┌──────┐┌──────┐                                           │
│  │Venue ││Venue ││Venue │  (SPSC queues to each adapter)            │
│  │Adpt 1││Adpt 2││Adpt N│                                           │
│  └──┬───┘└──┬───┘└──┬───┘                                           │
│     │      │      │                                                 │
├─────┼──────┼──────┼─────────────────────────────────────────────────┤
│     ▼      ▼      ▼       EXECUTION REPORTS (MPSC)                  │
│  ┌──────────────────────────┐                                       │
│  │   EXECUTION HANDLER      │  State machine, fill aggregation      │
│  │  ┌──────────┐ ┌────────┐ │                                       │
│  │  │State Mach│ │Fill Mgr│ │                                       │
│  │  └──────────┘ └────────┘ │                                       │
│  └──────────────────────────┘                                       │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────┐       │
│  │ INFRASTRUCTURE: Logging (spdlog) │ Metrics (Prometheus)  │       │
│  │ Config (YAML) │ Tracing (per-order context)              │       │
│  └──────────────────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────────────────┘
```

# Component Diagram (Mermaid)

```mermaid
graph TB
    subgraph Gateway
        FIX[FIX Gateway]
        API[API Gateway]
    end

    subgraph Risk
        RM[Risk Manager]
        RL[Rate Limiter]
        KS[Kill Switch]
    end

    subgraph MarketData
        AGG[Aggregator]
        BOOK[Order Book]
        FEED[Feed Handler]
        REPLAY[Replay Engine]
    end

    subgraph Routing
        ENG[Routing Engine]
        BP[Best Price]
        LS[Liquidity Sweep]
        IOC[Smart IOC]
        VWAP[VWAP]
    end

    subgraph Connectors
        SIM[Simulated Exchange]
        FIXV[FIX Adapter]
    end

    subgraph Execution
        EH[Execution Handler]
        FM[Fill Manager]
        SM[State Machine]
    end

    subgraph Infra
        LOG[Logging]
        MET[Metrics]
        CFG[Config]
        TR[Tracing]
    end

    FIX --> RM
    API --> RM
    RM --> ENG
    AGG --> ENG
    ENG --> SIM
    ENG --> FIXV
    SIM --> EH
    FIXV --> EH
    EH --> FM
    EH --> SM
```
