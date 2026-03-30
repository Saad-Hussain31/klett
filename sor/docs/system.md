# Smart Order Router -- System Documentation

## 1. Introduction

A Smart Order Router (SOR) is a core piece of electronic trading infrastructure
responsible for deciding **where** and **how** to execute client orders across
multiple trading venues. This document explains the business problem, the
relevant market microstructure, and the role the SOR plays in achieving best
execution.

---

## 2. Business Problem

### 2.1 Best Execution Obligation

Every broker-dealer and investment firm that handles client orders is subject
to a **best execution obligation**: the duty to obtain the most favorable
terms reasonably available for the client's transaction, taking into account
price, cost, speed, likelihood of execution, and settlement.

Best execution is not simply about finding the lowest ask or highest bid.
It is a multi-dimensional optimization across:

| Factor              | Description                                                  |
|---------------------|--------------------------------------------------------------|
| **Price**           | The most favorable price available across all venues.        |
| **Cost**            | Explicit fees (exchange fees, clearing) and implicit costs (spread, market impact). |
| **Speed**           | Latency of order acknowledgement, fill, and reporting.       |
| **Likelihood**      | Probability that the order will be filled at the quoted price and size. |
| **Settlement**      | Reliability and timeliness of post-trade settlement.         |
| **Market impact**   | How much the act of trading moves the price against the client. |

A human trader cannot manually evaluate these factors in real time across
dozens of venues, hundreds of symbols, and thousands of price levels. An SOR
automates this decision-making process.

### 2.2 Regulatory Requirements

#### Regulation NMS (United States)

Regulation National Market System (Reg NMS), adopted by the SEC in 2005 and
fully implemented by 2007, is the primary U.S. framework governing equity
market structure. Its key provisions relevant to SOR design are:

- **Order Protection Rule (Rule 611)** -- prohibits trade-throughs. A trading
  center must not execute a trade at a price inferior to a protected quotation
  displayed by another trading center. This means an SOR must be aware of the
  National Best Bid and Offer (NBBO) at all times and route orders to the
  venue(s) offering the best price.

- **Access Rule (Rule 610)** -- limits access fees that venues can charge for
  accessing protected quotations (currently capped at $0.003 per share for
  equities). This makes fee-adjusted price comparisons essential.

- **Sub-Penny Rule (Rule 612)** -- prohibits quotations in increments smaller
  than one cent for stocks priced above $1.00. This constrains the price
  granularity the SOR must handle.

- **Market Data Rules (Rules 601, 602, 603)** -- require the consolidation and
  dissemination of quotation and trade data through Securities Information
  Processors (SIPs), ensuring all participants can observe the NBBO.

- **Consolidated Audit Trail (CAT)** -- requires broker-dealers and exchanges
  to report full order lifecycle events (creation, routing, modification,
  cancellation, execution) to a central repository. The SOR's tracing
  infrastructure directly supports CAT reporting obligations.

#### MiFID II (European Union)

The Markets in Financial Instruments Directive II (MiFID II), effective
January 2018, imposes similar but broader obligations across EU member states:

- **Best Execution (Article 27)** -- firms must take "all sufficient steps"
  to obtain the best possible result for clients, considering price, costs,
  speed, likelihood of execution and settlement, size, nature, or any other
  relevant consideration.

- **Execution Policy** -- firms must establish and publish an order execution
  policy describing how best execution is achieved, including the venues used
  and the factors weighed in routing decisions.

- **Transaction Reporting (Article 26)** -- every executed transaction must be
  reported to the relevant competent authority, including the venue of
  execution, precise timestamps, and counterparty identifiers.

- **RTS 27 / RTS 28** -- Regulatory Technical Standards requiring venues to
  publish execution quality statistics and firms to publish annual reports on
  their top five execution venues per instrument class.

- **Systematic Internaliser (SI) Regime** -- firms that deal on own account
  on an organized, frequent, and systematic basis must comply with pre-trade
  transparency and quote obligations, creating an additional class of
  execution venue the SOR must consider.

- **Clock Synchronization (RTS 25)** -- trading venues and their members must
  synchronize clocks to UTC with granularity of at least one millisecond
  (100 microseconds for high-frequency activity). The SOR timestamps all
  events accordingly.

#### Other Jurisdictions

Similar best execution and market structure regulations exist worldwide:

- **Canada**: UMIR (Universal Market Integrity Rules) and NI 23-101
  (Trading Rules), requiring best execution and order protection analogous to
  Reg NMS.
- **Australia**: ASIC Market Integrity Rules, with best execution obligations
  and a competitive multi-venue market structure.
- **Japan**: JSDA Best Execution Policy guidelines for broker-dealers.
- **Hong Kong**: SFC Code of Conduct, requiring reasonable steps to obtain
  best execution.

The common thread across all jurisdictions is that routing decisions must be
demonstrably in the client's interest, recorded for audit, and reproducible
upon regulatory inquiry.

---

## 3. Market Microstructure

### 3.1 Order Books

The fundamental data structure of an electronic exchange is the **limit order
book** (LOB). It is a collection of outstanding buy orders (bids) and sell
orders (asks), organized by price level:

```
        Bids (buy orders)              Asks (sell orders)
   Price     Qty   Orders          Price     Qty   Orders
  --------  -----  ------         --------  -----  ------
   150.03     800      3           150.04     500      2
   150.02    1200      5           150.05     300      1
   150.01    2000     12           150.06    1500      7
   150.00    5000     28           150.07     800      4
```

Key concepts:

- The **best bid** is the highest price any buyer is willing to pay (150.03).
- The **best ask** (or best offer) is the lowest price any seller is willing
  to accept (150.04).
- The **spread** is the difference between the best ask and the best bid
  (150.04 - 150.03 = $0.01).
- The **midpoint** is the average of the best bid and best ask
  ((150.03 + 150.04) / 2 = $150.035).
- **Depth** refers to the total quantity available at or beyond the top of
  book. Deeper books can absorb larger orders with less price impact.

A **Level 1 (L1)** feed provides only the best bid and ask (top-of-book).
A **Level 2 (L2)** feed provides multiple price levels of depth, allowing the
SOR to estimate available liquidity at each level and plan sweep strategies.
A **Level 3 (L3)** feed provides individual order information within each
price level (available only from some venues).

### 3.2 Price-Time Priority

Most exchanges use **price-time priority** (also called FIFO matching) as
their order matching algorithm:

1. Incoming marketable orders are matched against resting limit orders.
2. The best-priced resting order has highest priority.
3. Among orders at the same price, the one that arrived earliest is filled
   first.

This means that placing an order early at a given price level provides a queue
priority advantage. Some venues use alternative priority models:

- **Pro-rata**: quantity at a price level determines allocation (common in
  futures, e.g., CME Eurodollar).
- **Price-size-time**: larger orders at the same price get priority (some
  options exchanges).
- **Price-broker-time**: orders from the same broker are consolidated for
  priority (certain Asian exchanges).

The SOR must understand each venue's matching model to predict fill probability
and plan routing accordingly.

### 3.3 Order Types and Time-in-Force

Common order types the SOR must handle:

| Order Type       | Behavior                                              |
|------------------|-------------------------------------------------------|
| **Limit**        | Rests on the book at the specified price until filled, canceled, or expired. |
| **Market**       | Executes immediately at the best available price. No price protection. |
| **IOC**          | Immediate-or-Cancel: fill what is available now, cancel the rest. |
| **FOK**          | Fill-or-Kill: fill the entire quantity immediately, or cancel the whole order. |

Common time-in-force instructions:

| TIF    | Meaning                                                  |
|--------|----------------------------------------------------------|
| **GTC**| Good-Til-Canceled: remains active until explicitly canceled. |
| **DAY**| Expires at the end of the trading day.                   |
| **IOC**| Immediate-or-Cancel (same as the IOC order type).        |
| **FOK**| Fill-or-Kill (same as the FOK order type).               |
| **GTD**| Good-Til-Date: expires at a specified future date/time.  |

### 3.4 Lit vs. Dark Venues

Trading venues fall into two broad categories:

**Lit venues** (exchanges, displayed markets):
- Pre-trade transparency: all resting orders and their prices are visible.
- Examples: NYSE, NASDAQ, CBOE EDGX, Euronext, LSE, Deutsche Boerse.
- SOR can observe the order book and make informed routing decisions.
- Subject to Order Protection Rule (no trade-throughs).

**Dark venues** (dark pools, non-displayed markets):
- No pre-trade transparency: resting orders are hidden.
- Trades typically execute at or near the midpoint of the NBBO.
- Advantages: reduced market impact, potential price improvement.
- Disadvantages: uncertain fill probability, potential information leakage.
- Examples: Crossfinder, SIGMA X, Liquidnet, POSIT.

**Crossing networks** are a subset of dark venues where buy and sell orders
are matched at periodic intervals (e.g., at the NBBO midpoint) rather than
continuously.

**Internalization** occurs when a broker-dealer fills a client order from its
own inventory rather than routing to an external venue. Internalizers may offer
price improvement (filling at the midpoint rather than the NBBO) but introduce
conflicts of interest.

An effective SOR considers all venue types, probing dark pools for midpoint
liquidity before sweeping displayed markets.

### 3.5 Maker/Taker Fee Model

Most U.S. equity exchanges operate on a **maker-taker fee model**:

- **Takers** (aggressive orders that remove liquidity) pay a fee.
  Typical: $0.0025 - $0.0030 per share.
- **Makers** (passive orders that add liquidity) receive a rebate.
  Typical: $0.0020 - $0.0028 per share.

Some venues use an **inverted fee model** (taker rebate, maker fee) to attract
aggressive order flow. The fee differential can make a venue with a nominally
worse price actually cheaper after fees.

Example:

| Venue   | Ask Price | Taker Fee  | Fee-Adjusted Cost |
|---------|-----------|------------|-------------------|
| NYSE    | $150.04   | +$0.0030   | $150.0430         |
| EDGX    | $150.05   | -$0.0020   | $150.0480         |
| BX      | $150.04   | +$0.0010   | $150.0410         |

Despite NYSE and BX showing the same ask price ($150.04), BX is cheaper after
fees. The SOR must compute **fee-adjusted prices** for every venue and route
to the venue with the lowest all-in cost.

### 3.6 Market Fragmentation

In the U.S., equity trading is fragmented across:

- 16 registered stock exchanges (NYSE, NASDAQ, CBOE BZX/BYX/EDGX/EDGA,
  IEX, MEMX, LTSE, MIAX Pearl, and others).
- ~30 alternative trading systems (ATSs) / dark pools.
- Numerous broker-dealer internalizers.
- Off-exchange wholesalers (for retail order flow).

As of recent data, approximately 50-55% of U.S. equity volume executes on
lit exchanges. The remaining volume splits among dark pools, internalizers,
and off-exchange venues.

In Europe, MiFID/MiFID II intentionally fragmented trading away from national
exchange monopolies, creating:

- **Regulated Markets (RMs)**: traditional exchanges.
- **Multilateral Trading Facilities (MTFs)**: alternative lit venues.
- **Systematic Internalisers (SIs)**: firms dealing on own account.
- **Organised Trading Facilities (OTFs)**: for non-equity instruments.

This fragmentation is the fundamental reason an SOR exists: no single venue
shows the complete picture. The best price for any given order at any given
moment could be on any of dozens of venues.

---

## 4. Why a Smart Order Router Is Needed

### 4.1 Fragmented Liquidity

When liquidity is spread across multiple venues, a naive approach of always
routing to one exchange will frequently:

- Miss better prices available elsewhere.
- Fail to fill large orders that could be satisfied by aggregating liquidity
  from several venues.
- Violate the Order Protection Rule by trading through protected quotes.

### 4.2 Information Asymmetry and Market Impact

Large orders reveal trading intent. If a fund needs to buy 100,000 shares and
sends the entire quantity to one venue, other participants observe the
aggressive demand and move prices higher before the order is fully filled.
This adverse price movement is called **market impact**.

An SOR mitigates market impact by:

- Splitting orders into smaller child orders across venues.
- Time-slicing execution over a target horizon (VWAP, TWAP strategies).
- Probing dark pools for hidden liquidity before lighting up public markets.
- Dynamically adjusting pace based on fill progress and market conditions.
- Randomizing slice sizes and timing to reduce predictability.

### 4.3 Speed

Market conditions change in microseconds. A quote that is best when the
routing decision is made may be gone by the time the order arrives at the
venue. This is known as **stale quote risk**. The SOR must:

- Minimize decision latency (microsecond-level routing logic).
- Minimize network latency (co-located venue connections where possible).
- Handle stale-quote detection and fallback routing.
- Account for venue-specific latency characteristics when scoring venues
  (a slightly worse price on a faster venue may yield better realized
  execution than the best price on a slow venue).

### 4.4 Cost Optimization

Beyond price, the SOR optimizes total execution cost:

- Selecting venues with lower fee-adjusted prices.
- Choosing passive (maker) execution when time permits, to earn rebates
  instead of paying taker fees.
- Avoiding "dust" orders (very small slices) that incur minimum fee charges
  without meaningful execution benefit.
- Balancing information leakage cost against immediacy.

### 4.5 Regulatory Compliance

The SOR must maintain an auditable record of every routing decision, including:

- The market data snapshot at the time of the decision (NBBO, depth).
- The venues considered, their prices, fees, and latencies.
- The reason a particular venue was selected or rejected.
- Timestamps with microsecond or better precision.
- The strategy used and its parameters.

This audit trail is essential for demonstrating best execution compliance
during regulatory examinations and for generating the RTS 28 / Rule 606
reports required by MiFID II and SEC regulation respectively.

---

## 5. SOR Responsibilities

### 5.1 Order Routing

The primary function: receive a parent order from the client and determine the
optimal way to execute it. This involves:

1. **Market data aggregation** -- Consolidating order books from all connected
   venues to form a unified view (NBBO plus aggregated depth).
2. **Venue evaluation** -- Scoring each venue on price, fees, latency, fill
   rate, and current availability.
3. **Strategy selection** -- Choosing the appropriate routing algorithm based
   on the order's type, size, urgency, and time-in-force.
4. **Child order generation** -- Splitting the parent order into one or more
   child orders (slices) targeted at specific venues.
5. **Dispatch** -- Sending child orders to venues through the appropriate
   protocol adapters (FIX, native binary, etc.).

### 5.2 Execution Quality Management

Beyond initial routing, the SOR monitors execution quality in real time:

- Tracking partial fills and adjusting remaining quantity allocation.
- Detecting child order rejections and rerouting to alternative venues.
- Measuring realized slippage against the arrival price benchmark.
- Recording fill quality metrics for post-trade analysis.
- Aggregating fills from child orders back to the parent order.
- Triggering completion when the parent order is fully filled.

### 5.3 Risk Management

Pre-trade risk checks prevent orders that would violate:

- Maximum order size limits (quantity and notional value).
- Maximum position limits (per-symbol and portfolio-wide).
- Rate limits (orders per second, to avoid exchange throttling).
- Loss limits (maximum unrealized plus realized loss threshold).
- Kill switch (global circuit breaker for emergency halt).

Risk checks are ordered from cheapest (atomic load for kill switch) to most
expensive (position map lookup) to minimize latency on the critical path.
The first check to fail short-circuits the remaining checks.

### 5.4 Smart Routing Algorithms

The SOR implements multiple routing strategies, each optimized for different
execution objectives:

| Strategy           | Objective                                      | Use Case                         |
|--------------------|------------------------------------------------|----------------------------------|
| **BestPrice**      | Route to the single venue with the best fee-adjusted price. Tie-breaks on latency and fill rate. | Small orders, price-sensitive flow. |
| **LiquiditySweep** | Sweep liquidity across multiple venues simultaneously, walking the aggregated book from best to worst price. | Large orders needing immediate execution. |
| **SmartIOC**       | Aggressive IOC/FOK with configurable slippage tolerance and latency-weighted venue preference. | Time-critical orders, IOC/FOK order types. |
| **VWAP**           | Time-slice over a target duration, dynamically adjusting pace based on fill progress and urgency. | Large orders minimizing market impact over a horizon. |

### 5.5 Monitoring and Observability

Production SOR systems require:

- **Metrics**: order counts, fill rates, latencies (per-venue and end-to-end),
  rejection rates, active order counts, position levels. Exposed via
  Prometheus-compatible endpoint.
- **Tracing**: per-order lifecycle timeline from submission through fill,
  with microsecond timestamps at each stage (submit, risk_check, route,
  venue_send, fill).
- **Logging**: structured, leveled logs (trace/debug/info/warn/error/critical)
  via spdlog for operational troubleshooting.
- **Alerting**: kill switch activation, venue disconnects, stale market data,
  position limit breaches, abnormal rejection rates.

---

## 6. Key Performance Indicators

An SOR is evaluated against several execution quality benchmarks:

| KPI                     | Definition                                           |
|-------------------------|------------------------------------------------------|
| **Effective spread**    | 2 * |trade_price - midpoint| at time of execution.   |
| **Implementation shortfall** | Difference between the decision price and the realized average execution price. |
| **Fill rate**           | Percentage of order quantity successfully executed.   |
| **Venue hit rate**      | Per-venue percentage of routed orders that result in fills. |
| **End-to-end latency**  | Time from order receipt to venue acknowledgement.    |
| **Routing latency**     | Time spent in the routing decision engine.           |
| **VWAP slippage**       | Difference between realized average price and market VWAP. |

---

## 7. Summary

The Smart Order Router sits at the intersection of market microstructure,
regulatory compliance, and low-latency software engineering. Its purpose is
to automate the best execution obligation by continuously evaluating
fragmented liquidity across venues, applying risk controls, and executing
orders through the strategy best suited to each order's characteristics.

The remainder of the documentation suite covers the specific architecture
of this implementation, a developer guide for building and extending the
system, and an API reference for the key classes.

---

*See also: [Architecture](architecture.md) | [Developer Guide](developer_guide.md) | [API Reference](api_reference.md)*
