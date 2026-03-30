"""Scenario runner for structured backtesting.

Defines composable Scenario objects that specify market conditions,
order flow, and expected outcomes, then runs them against the simulator
and reports pass/fail together with execution metrics.
"""

from dataclasses import dataclass, field
from typing import Optional

import numpy as np

from ..simulation.market_simulator import MarketSimulator, VenueConfig
from ..simulation.order_book import Side


@dataclass
class Scenario:
    """Defines a single test scenario for the SOR."""
    name: str
    description: str = ""
    venues: list[VenueConfig] = field(default_factory=list)
    start_price: float = 100.0
    volatility: float = 0.001
    warmup_ticks: int = 10
    orders: list[dict] = field(default_factory=list)
    routing_strategy: str = "best_price"
    max_slippage_bps: Optional[float] = None
    min_fill_rate: Optional[float] = None
    max_avg_latency_us: Optional[float] = None
    max_total_fees: Optional[float] = None


@dataclass
class ScenarioResult:
    """Result of running a single scenario."""
    scenario_name: str
    passed: bool = True
    failures: list[str] = field(default_factory=list)
    total_orders: int = 0
    filled_orders: int = 0
    fill_rate: float = 0.0
    avg_fill_price: float = 0.0
    total_fees: float = 0.0
    slippage_bps: float = 0.0
    avg_latency_us: float = 0.0


class ScenarioRunner:
    """Runs a batch of scenarios and reports results."""

    def __init__(self):
        self.scenarios: list[Scenario] = []

    def add(self, scenario: Scenario):
        self.scenarios.append(scenario)

    def run(self, scenario: Scenario) -> ScenarioResult:
        """Run a single scenario and return the result."""
        venues = scenario.venues or [
            VenueConfig(venue_id=1, name="VenueA", fee_rate=0.001, latency_us=50),
            VenueConfig(venue_id=2, name="VenueB", fee_rate=0.0008, latency_us=80),
            VenueConfig(venue_id=3, name="VenueC", fee_rate=0.0012, latency_us=30),
        ]

        sim = MarketSimulator(symbol="TEST", venues=venues)
        sim.mid_price = scenario.start_price
        sim.generate_market_data(
            num_ticks=scenario.warmup_ticks,
            volatility=scenario.volatility,
        )

        result = ScenarioResult(scenario_name=scenario.name)
        fill_prices, fill_qtys, arrival_prices, latencies = [], [], [], []

        for order_spec in scenario.orders:
            side = Side.BUY if order_spec.get("side", "buy").lower() == "buy" else Side.SELL
            quantity = float(order_spec.get("quantity", 100))
            price = order_spec.get("price")

            nbbo = sim.get_nbbo()
            arrival = nbbo.get("best_ask") if side == Side.BUY else nbbo.get("best_bid")
            if arrival:
                arrival_prices.append(arrival)

            result.total_orders += 1
            routed = sim.route_order(side=side, quantity=quantity, price=price,
                                     strategy=scenario.routing_strategy)

            for ro in routed:
                if ro.order.filled_qty > 0:
                    result.filled_orders += 1
                    for f in ro.fills:
                        fill_prices.append(f.price)
                        fill_qtys.append(f.quantity)
                    latencies.append((ro.ack_time - ro.route_time) * 1_000_000)
                    result.total_fees += ro.fees

            sim.step()

        # Compute metrics
        if fill_qtys:
            fp, fq = np.array(fill_prices), np.array(fill_qtys)
            result.avg_fill_price = float(np.sum(fp * fq) / np.sum(fq))
        if result.total_orders > 0:
            result.fill_rate = result.filled_orders / result.total_orders
        if latencies:
            result.avg_latency_us = float(np.mean(latencies))
        if arrival_prices and fill_prices:
            mean_arrival = np.mean(arrival_prices[:len(fill_prices)])
            if mean_arrival > 0:
                result.slippage_bps = float(
                    (result.avg_fill_price - mean_arrival) / mean_arrival * 10000
                )

        # Check acceptance criteria
        result.passed = True
        checks = [
            (scenario.max_slippage_bps, abs(result.slippage_bps),
             f"Slippage {result.slippage_bps:.2f} bps > max {scenario.max_slippage_bps}"),
            (scenario.max_avg_latency_us, result.avg_latency_us,
             f"Latency {result.avg_latency_us:.1f} us > max {scenario.max_avg_latency_us}"),
            (scenario.max_total_fees, result.total_fees,
             f"Fees {result.total_fees:.4f} > max {scenario.max_total_fees}"),
        ]
        for threshold, value, msg in checks:
            if threshold is not None and value > threshold:
                result.passed = False
                result.failures.append(msg)
        if scenario.min_fill_rate is not None and result.fill_rate < scenario.min_fill_rate:
            result.passed = False
            result.failures.append(
                f"Fill rate {result.fill_rate:.2%} < min {scenario.min_fill_rate:.2%}")

        return result

    def run_all(self) -> list[ScenarioResult]:
        return [self.run(s) for s in self.scenarios]

    @staticmethod
    def print_report(results: list[ScenarioResult]):
        print("=" * 70)
        print("SCENARIO TEST REPORT")
        print("=" * 70)
        passed = sum(1 for r in results if r.passed)
        print(f"  {passed}/{len(results)} scenarios passed\n")
        for r in results:
            status = "PASS" if r.passed else "FAIL"
            print(f"  [{status}] {r.scenario_name}")
            print(f"         Orders: {r.total_orders}  Fills: {r.filled_orders}  "
                  f"Rate: {r.fill_rate:.2%}  Slippage: {r.slippage_bps:.2f} bps")
            for f in r.failures:
                print(f"         >> {f}")
            print()
        print("=" * 70)


def basic_buy_scenario() -> Scenario:
    return Scenario(
        name="basic_buy", description="10 market buy orders",
        orders=[{"side": "buy", "quantity": 100} for _ in range(10)],
        routing_strategy="best_price", min_fill_rate=0.8, max_slippage_bps=50.0,
    )

def high_volume_sweep_scenario() -> Scenario:
    return Scenario(
        name="high_volume_sweep", description="Large sweep across venues",
        orders=[{"side": "buy", "quantity": 5000}],
        routing_strategy="split_pro_rata", min_fill_rate=0.5,
    )

def volatile_market_scenario() -> Scenario:
    return Scenario(
        name="volatile_market", description="Orders during high volatility",
        volatility=0.01, warmup_ticks=50,
        orders=[{"side": "buy", "quantity": 200} for _ in range(20)],
        routing_strategy="best_price", max_slippage_bps=100.0,
    )
