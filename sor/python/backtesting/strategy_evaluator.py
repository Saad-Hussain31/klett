"""Strategy performance evaluation and comparison."""

from dataclasses import dataclass, field
from typing import Optional

import numpy as np

from ..simulation.market_simulator import MarketSimulator, VenueConfig
from ..simulation.order_book import Side, OrderType


@dataclass
class StrategyResult:
    """Performance metrics for a single strategy evaluation."""
    strategy_name: str
    total_orders: int = 0
    total_fills: int = 0
    total_quantity: float = 0.0
    total_notional: float = 0.0
    avg_fill_price: float = 0.0
    vwap: float = 0.0
    total_fees: float = 0.0
    avg_latency_us: float = 0.0
    fill_rate: float = 0.0
    slippage_bps: float = 0.0
    implementation_shortfall: float = 0.0
    p50_latency_us: float = 0.0
    p99_latency_us: float = 0.0
    min_fill_price: float = 0.0
    max_fill_price: float = 0.0


class StrategyEvaluator:
    """Evaluates and compares routing strategy performance.

    Uses a MarketSimulator to test strategies against simulated market
    conditions and computes comprehensive performance metrics.
    """

    def __init__(self, simulator: MarketSimulator):
        self.simulator = simulator
        self.results: dict[str, StrategyResult] = {}

    def evaluate_strategy(
        self,
        strategy_name: str,
        orders: list[dict],
        num_simulations: int = 100,
    ) -> StrategyResult:
        """Run a strategy through multiple simulation passes.

        Args:
            strategy_name: Name of routing strategy ('best_price', 'sweep', etc.)
            orders: List of order dicts with keys: side, quantity, price.
            num_simulations: Number of Monte Carlo passes.

        Returns:
            Aggregated StrategyResult.
        """
        all_fill_prices = []
        all_fill_qtys = []
        all_latencies = []
        total_orders = 0
        total_fills_count = 0
        total_fees = 0.0
        arrival_prices = []

        for sim_run in range(num_simulations):
            # Repopulate books for each simulation
            self.simulator.populate_books()

            for order in orders:
                side_str = order.get("side", "buy").lower()
                side = Side.BUY if side_str == "buy" else Side.SELL
                quantity = float(order.get("quantity", 100))
                price = order.get("price")

                # Record arrival price (NBBO at time of order)
                nbbo = self.simulator.get_nbbo()
                if side == Side.BUY:
                    arrival = nbbo.get("best_ask", 0)
                else:
                    arrival = nbbo.get("best_bid", 0)
                arrival_prices.append(arrival)

                total_orders += 1

                # Route order
                fills = self.simulator.route_order(
                    side=side,
                    quantity=quantity,
                    price=price,
                    strategy=strategy_name,
                )

                if fills:
                    total_fills_count += 1
                    for f in fills:
                        all_fill_prices.append(f.price)
                        all_fill_qtys.append(f.quantity)
                        # Estimate latency from venue config
                        latency = 50.0  # default
                        all_latencies.append(latency)
                        total_fees += f.quantity * 0.001  # default fee

            # Step sim to next state
            self.simulator.step()

        # Compute metrics
        result = StrategyResult(strategy_name=strategy_name)
        result.total_orders = total_orders
        result.total_fills = total_fills_count

        if all_fill_qtys:
            result.total_quantity = sum(all_fill_qtys)
            fill_prices_arr = np.array(all_fill_prices)
            fill_qtys_arr = np.array(all_fill_qtys)

            result.total_notional = float(np.sum(fill_prices_arr * fill_qtys_arr))
            result.vwap = float(
                np.sum(fill_prices_arr * fill_qtys_arr) / np.sum(fill_qtys_arr)
            )
            result.avg_fill_price = float(np.mean(fill_prices_arr))
            result.min_fill_price = float(np.min(fill_prices_arr))
            result.max_fill_price = float(np.max(fill_prices_arr))
            result.total_fees = total_fees
            result.fill_rate = total_fills_count / total_orders if total_orders > 0 else 0

            if all_latencies:
                lat_arr = np.array(all_latencies)
                result.avg_latency_us = float(np.mean(lat_arr))
                result.p50_latency_us = float(np.percentile(lat_arr, 50))
                result.p99_latency_us = float(np.percentile(lat_arr, 99))

            # Slippage vs arrival price (in basis points)
            if arrival_prices:
                arr_prices = np.array(arrival_prices[:total_fills_count])
                if len(arr_prices) > 0 and np.mean(arr_prices) > 0:
                    slippage = (result.avg_fill_price - np.mean(arr_prices)) / np.mean(arr_prices)
                    result.slippage_bps = float(slippage * 10000)

            result.implementation_shortfall = result.slippage_bps * result.total_notional / 10000

        self.results[strategy_name] = result
        return result

    def compare_strategies(
        self,
        strategies: list[str],
        orders: list[dict],
        num_simulations: int = 100,
    ) -> dict:
        """Compare multiple strategies side by side.

        Returns:
            Dict with strategy names as keys and StrategyResult as values.
        """
        results = {}
        for strategy in strategies:
            results[strategy] = self.evaluate_strategy(
                strategy, orders, num_simulations
            )
        return results

    def generate_report(self, output_path: Optional[str] = None) -> str:
        """Generate a text performance report."""
        lines = []
        lines.append("=" * 80)
        lines.append("STRATEGY PERFORMANCE COMPARISON REPORT")
        lines.append("=" * 80)
        lines.append("")

        for name, result in self.results.items():
            lines.append(f"Strategy: {name}")
            lines.append("-" * 40)
            lines.append(f"  Total Orders:      {result.total_orders:>12d}")
            lines.append(f"  Total Fills:       {result.total_fills:>12d}")
            lines.append(f"  Fill Rate:         {result.fill_rate:>12.2%}")
            lines.append(f"  Total Quantity:    {result.total_quantity:>12.0f}")
            lines.append(f"  Total Notional:    {result.total_notional:>12.2f}")
            lines.append(f"  VWAP:              {result.vwap:>12.4f}")
            lines.append(f"  Avg Fill Price:    {result.avg_fill_price:>12.4f}")
            lines.append(f"  Min Fill Price:    {result.min_fill_price:>12.4f}")
            lines.append(f"  Max Fill Price:    {result.max_fill_price:>12.4f}")
            lines.append(f"  Total Fees:        {result.total_fees:>12.4f}")
            lines.append(f"  Slippage (bps):    {result.slippage_bps:>12.2f}")
            lines.append(f"  Impl Shortfall:    {result.implementation_shortfall:>12.4f}")
            lines.append(f"  Avg Latency (us):  {result.avg_latency_us:>12.1f}")
            lines.append(f"  P50 Latency (us):  {result.p50_latency_us:>12.1f}")
            lines.append(f"  P99 Latency (us):  {result.p99_latency_us:>12.1f}")
            lines.append("")

        report = "\n".join(lines)
        if output_path:
            with open(output_path, "w") as f:
                f.write(report)

        return report
