# Order State Machine Diagram

```mermaid
stateDiagram-v2
    [*] --> NEW: Order Created

    NEW --> PENDING_ACCEPT: Submit to venue
    NEW --> REJECTED: Validation / Risk fail

    PENDING_ACCEPT --> ACCEPTED: Venue ACK
    PENDING_ACCEPT --> REJECTED: Venue Reject

    ACCEPTED --> PARTIALLY_FILLED: Partial execution
    ACCEPTED --> FILLED: Full execution
    ACCEPTED --> PENDING_CANCEL: Cancel requested
    ACCEPTED --> REJECTED: Late reject

    PARTIALLY_FILLED --> PARTIALLY_FILLED: Another partial fill
    PARTIALLY_FILLED --> FILLED: Final fill
    PARTIALLY_FILLED --> PENDING_CANCEL: Cancel requested

    PENDING_CANCEL --> CANCELED: Cancel confirmed
    PENDING_CANCEL --> FILLED: Filled before cancel processed
    PENDING_CANCEL --> PARTIALLY_FILLED: Partial fill before cancel

    FILLED --> [*]
    CANCELED --> [*]
    REJECTED --> [*]
```

# State Transition Table

```
From State          | Event              | To State           | Notes
--------------------|--------------------|--------------------|------------------
NEW                 | submit             | PENDING_ACCEPT     |
NEW                 | reject             | REJECTED           | Risk/validation
PENDING_ACCEPT      | ack                | ACCEPTED           |
PENDING_ACCEPT      | reject             | REJECTED           | Venue reject
ACCEPTED            | partial_fill       | PARTIALLY_FILLED   |
ACCEPTED            | fill               | FILLED             |
ACCEPTED            | cancel_request     | PENDING_CANCEL     |
PARTIALLY_FILLED    | partial_fill       | PARTIALLY_FILLED   |
PARTIALLY_FILLED    | fill               | FILLED             |
PARTIALLY_FILLED    | cancel_request     | PENDING_CANCEL     |
PENDING_CANCEL      | cancel_ack         | CANCELED           |
PENDING_CANCEL      | fill               | FILLED             | Race condition
PENDING_CANCEL      | partial_fill       | PARTIALLY_FILLED   | Race condition
FILLED              | (terminal)         | -                  |
CANCELED            | (terminal)         | -                  |
REJECTED            | (terminal)         | -                  |
```

# ASCII State Machine

```
                    ┌─────────┐
                    │   NEW   │
                    └────┬────┘
              ┌──────────┼──────────┐
              ▼                     ▼
      ┌──────────────┐        ┌──────────┐
      │PENDING_ACCEPT│        │ REJECTED │◄─────────┐
      └──────┬───────┘        └──────────┘          │
        ┌────┼────┐                                 │
        ▼         ▼                                 │
  ┌──────────┐  (reject)────────────────────────────┘
  │ ACCEPTED │
  └────┬─────┘
  ┌────┼────────────┐
  ▼    ▼            ▼
┌────┐┌────────────┐┌──────────────┐
│FILL││PARTIAL_FILL││PENDING_CANCEL│
│  ED││   ◄────►   ││             │
└────┘└─────┬──────┘└──────┬──────┘
            │              │
            ▼              ▼
         ┌──────┐    ┌──────────┐
         │FILLED│    │ CANCELED │
         └──────┘    └──────────┘
```
