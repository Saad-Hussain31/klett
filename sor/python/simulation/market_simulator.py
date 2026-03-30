"""Multi-venue market simulator for SOR testing.

Simulates multiple independent exchanges each with their own order book,
latency, fee structure, and fill probability. Generates correlated market
data using Geometric Brownian Motion with venue-specific noise.
"""

import random
import time
from dataclasses import dataclass, field
from typing import Optional

import numpy as np

from .order_book import Fill, OrderBookSimulator, OrderStatus, OrderType, Side, SimOrder


@dataclass
class VenueConfig:
    """Configuration for a simulated venue."""
    venue_id: int
    name: str
    fee_rate: float = 0.001        # 10 bps
    latency_us: float = 50.0       # one-way latency in microseconds
    fill_probability: float = 0.95  # probability a matched order actually fills


@dataclass
class RoutedOrder:
    """Tracks an order routed to a specific venue."""
    venue_id: int
    order: SimOrder
    route_time: float = 0.0
    ack_time: float = 0.0
    fills: list = field(default_factory=list)
    fees: float = 0.0


class MarketSimulator:
    """Simulates multiple venues with independent order books.

    Each venue has its own order book, fee schedule, latency profile, and
    fill probability. The simulator drives a shared mid-price using GBM
    and adds venue-specific noise to create realistic cross-venue dynamics.
    """

    def __init__(self, symbol: str, venues: list[VenueConfig]):
        self.symbol = symbol
        self.venues: dict[int, VenueConfig] = {v.venue_id: v for v in venues}
        self.books: dict[int, OrderBookSimulator] = {
            v.venue_id: OrderBookSimulator(symbol) for v in venues
        }
        self.mid_price: float = 100.0  # starting price
        self._price_history: list[float] = [self.mid_price]
        self._tick_count: int = 0
        self._routed_orders: list[RoutedOrder] = []
        self._rng = np.random.default_rng(42)

    # ------------------------------------------------------------------
    # Market data generation
    # ------------------------------------------------------------------

    def generate_market_data(
        self,
        num_ticks: int,
        volatility: float = 0.001,
        drift: float = 0.0,
        dt: float = 1.0,
    ) -> list[float]:
        """Generate correlated market data across venues using GBM.

        Drives the shared mid-price with Geometric Brownian Motion and
        populates each venue's book at every tick with venue-specific noise.

        Args:
            num_ticks: Number of price ticks to generate.
            volatility: Annualized volatility (scaled by sqrt(dt)).
            drift: Drift term per tick.
            dt: Time-step size for the GBM discretization.

        Returns:
            List of mid-prices over time.
        """
        prices: list[float] = []
        price = self.mid_price

        for _ in range(num_ticks):
            # GBM step: dS = S * (mu*dt + sigma*sqrt(dt)*Z)
            z = self._rng.standard_normal()
            price *= np.exp((drift - 0.5 * volatility**2) * dt + volatility * np.sqrt(dt) * z)
            price = round(max(price, 0.01), 2)  # floor at 1 cent
            self.mid_price = price
            prices.append(price)
            self._price_history.append(price)

            # Populate each venue's book around the new mid price
            self.populate_books(depth=5)

        return prices

    def populate_books(self, depth: int = 10):
        """Populate all venue books with realistic liquidity.

        Clears existing resting orders and creates *depth* levels on each
        side with randomized quantities. Each venue gets a slightly
        different spread and quantity profile.
        """
        for venue_id, book in self.books.items():
            # Wipe old resting orders
            book.bids.clear()
            book.asks.clear()
            book.orders.clear()

            venue_cfg = self.venues[venue_id]
            # Venue-specific spread noise: wider for slower venues
            spread_noise = venue_cfg.latency_us / 100_000.0
            half_spread = 0.01 + spread_noise + self._rng.uniform(0, 0.005)

            for level in range(depth):
                offset = half_spread + level * book.tick_size

                bid_price = round(self.mid_price - offset, 2)
                ask_price = round(self.mid_price + offset, 2)

                # Quantity decreases with distance from mid; add randomness
                base_qty = max(10, int(1000 / (level + 1)))
                bid_qty = base_qty + int(self._rng.integers(-50, 50))
                ask_qty = base_qty + int(self._rng.integers(-50, 50))
                bid_qty = max(10, bid_qty)
                ask_qty = max(10, ask_qty)

                book.submit_order(Side.BUY, OrderType.LIMIT, bid_price, float(bid_qty))
                book.submit_order(Side.SELL, OrderType.LIMIT, ask_price, float(ask_qty))

    def get_nbbo(self) -> dict:
        """Get the National Best Bid and Offer across all venues.

        Returns:
            Dict with keys ``best_bid``, ``best_bid_venue``, ``best_bid_qty``,
            ``best_ask``, ``best_ask_venue``, ``best_ask_qty``, ``mid``, ``spread``.
        """
        best_bid: Optional[float] = None
        best_bid_venue: Optional[int] = None
        best_bid_qty: float = 0.0
        best_ask: Optional[float] = None
        best_ask_venue: Optional[int] = None
        best_ask_qty: float = 0.0

        for venue_id, book in self.books.items():
            bb = book.best_bid()
            ba = book.best_ask()

            if bb is not None:
                if best_bid is None or bb > best_bid:
                    best_bid = bb
                    best_bid_venue = venue_id
                    depth = book.get_depth(1)
                    best_bid_qty = depth["bids"][0][1] if depth["bids"] else 0.0

            if ba is not None:
                if best_ask is None or ba < best_ask:
                    best_ask = ba
                    best_ask_venue = venue_id
                    depth = book.get_depth(1)
                    best_ask_qty = depth["asks"][0][1] if depth["asks"] else 0.0

        mid = None
        spread = None
        if best_bid is not None and best_ask is not None:
            mid = round((best_bid + best_ask) / 2.0, 8)
            spread = round(best_ask - best_bid, 8)

        return {
            "best_bid": best_bid,
            "best_bid_venue": best_bid_venue,
            "best_bid_qty": best_bid_qty,
            "best_ask": best_ask,
            "best_ask_venue": best_ask_venue,
            "best_ask_qty": best_ask_qty,
            "mid": mid,
            "spread": spread,
        }

    # ------------------------------------------------------------------
    # Order routing
    # ------------------------------------------------------------------

    def route_order(
        self,
        side: Side,
        quantity: float,
        price: Optional[float] = None,
        strategy: str = "best_price",
    ) -> list[RoutedOrder]:
        """Route an order across venues using the specified strategy.

        Strategies:
            ``best_price`` - send the entire order to the venue with the
                best price on the relevant side.
            ``split_pro_rata`` - split the order across all venues
                proportional to available liquidity at the best price.
            ``lowest_fee`` - send to the venue with the lowest fee.
            ``round_robin`` - distribute equally across venues.

        Returns:
            List of RoutedOrder objects.
        """
        if strategy == "best_price":
            return self._route_best_price(side, quantity, price)
        elif strategy == "split_pro_rata":
            return self._route_pro_rata(side, quantity, price)
        elif strategy == "lowest_fee":
            return self._route_lowest_fee(side, quantity, price)
        elif strategy == "round_robin":
            return self._route_round_robin(side, quantity, price)
        else:
            raise ValueError(f"Unknown routing strategy: {strategy}")

    def step(self):
        """Advance the simulation by one tick.

        Applies a small random walk to the mid-price and refreshes venue books.
        """
        z = self._rng.standard_normal()
        self.mid_price *= np.exp(0.001 * z)
        self.mid_price = round(max(self.mid_price, 0.01), 2)
        self._price_history.append(self.mid_price)
        self._tick_count += 1
        self.populate_books(depth=5)

    # ------------------------------------------------------------------
    # Routing strategy implementations
    # ------------------------------------------------------------------

    def _route_best_price(
        self, side: Side, quantity: float, price: Optional[float]
    ) -> list[RoutedOrder]:
        """Send entire order to venue with best price."""
        best_venue_id = None
        best_price_val = None

        for venue_id, book in self.books.items():
            if side == Side.BUY:
                p = book.best_ask()
                if p is not None and (best_price_val is None or p < best_price_val):
                    best_price_val = p
                    best_venue_id = venue_id
            else:
                p = book.best_bid()
                if p is not None and (best_price_val is None or p > best_price_val):
                    best_price_val = p
                    best_venue_id = venue_id

        if best_venue_id is None:
            return []

        order_price = price if price is not None else best_price_val
        order_type = OrderType.LIMIT if price is not None else OrderType.MARKET
        return [self._send_to_venue(best_venue_id, side, order_type, order_price, quantity)]

    def _route_pro_rata(
        self, side: Side, quantity: float, price: Optional[float]
    ) -> list[RoutedOrder]:
        """Split order across venues proportional to available liquidity."""
        venue_liq: dict[int, float] = {}
        for venue_id, book in self.books.items():
            depth = book.get_depth(1)
            if side == Side.BUY and depth["asks"]:
                venue_liq[venue_id] = depth["asks"][0][1]
            elif side == Side.SELL and depth["bids"]:
                venue_liq[venue_id] = depth["bids"][0][1]

        total_liq = sum(venue_liq.values())
        if total_liq <= 0:
            return []

        results: list[RoutedOrder] = []
        remaining = quantity
        for venue_id, liq in venue_liq.items():
            share = quantity * (liq / total_liq)
            child_qty = min(round(share, 2), remaining)
            if child_qty <= 0:
                continue

            if side == Side.BUY:
                best_p = self.books[venue_id].best_ask()
            else:
                best_p = self.books[venue_id].best_bid()

            order_price = price if price is not None else best_p
            order_type = OrderType.LIMIT if price is not None else OrderType.MARKET
            results.append(self._send_to_venue(venue_id, side, order_type, order_price, child_qty))
            remaining -= child_qty

        return results

    def _route_lowest_fee(
        self, side: Side, quantity: float, price: Optional[float]
    ) -> list[RoutedOrder]:
        """Send entire order to venue with lowest fee rate."""
        best_venue_id = min(self.venues, key=lambda vid: self.venues[vid].fee_rate)

        if side == Side.BUY:
            best_p = self.books[best_venue_id].best_ask()
        else:
            best_p = self.books[best_venue_id].best_bid()

        if best_p is None:
            return []

        order_price = price if price is not None else best_p
        order_type = OrderType.LIMIT if price is not None else OrderType.MARKET
        return [self._send_to_venue(best_venue_id, side, order_type, order_price, quantity)]

    def _route_round_robin(
        self, side: Side, quantity: float, price: Optional[float]
    ) -> list[RoutedOrder]:
        """Distribute order equally across all venues."""
        venue_ids = list(self.venues.keys())
        if not venue_ids:
            return []

        child_qty = round(quantity / len(venue_ids), 2)
        results: list[RoutedOrder] = []
        remaining = quantity

        for i, venue_id in enumerate(venue_ids):
            qty = child_qty if i < len(venue_ids) - 1 else remaining
            if qty <= 0:
                continue

            if side == Side.BUY:
                best_p = self.books[venue_id].best_ask()
            else:
                best_p = self.books[venue_id].best_bid()

            if best_p is None:
                continue

            order_price = price if price is not None else best_p
            order_type = OrderType.LIMIT if price is not None else OrderType.MARKET
            results.append(self._send_to_venue(venue_id, side, order_type, order_price, qty))
            remaining -= qty

        return results

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    def _send_to_venue(
        self,
        venue_id: int,
        side: Side,
        order_type: OrderType,
        price: float,
        quantity: float,
    ) -> RoutedOrder:
        """Submit an order to a specific venue's book and record result."""
        cfg = self.venues[venue_id]
        book = self.books[venue_id]

        route_time = time.time()

        # Simulate latency
        latency_s = cfg.latency_us / 1_000_000.0

        # Simulate probabilistic fill: with (1 - fill_probability) the
        # order "misses" (e.g. quote moved), modeled by reducing quantity.
        effective_qty = quantity
        if self._rng.random() > cfg.fill_probability:
            effective_qty = round(quantity * self._rng.uniform(0.0, 0.5), 2)

        order = book.submit_order(side, order_type, price, effective_qty)
        ack_time = route_time + latency_s

        # Compute fees on filled notional
        filled_notional = sum(f.price * f.quantity for f in book.fills[-2:] if f.order_id == order.order_id and f.aggressor)
        fees = filled_notional * cfg.fee_rate

        routed = RoutedOrder(
            venue_id=venue_id,
            order=order,
            route_time=route_time,
            ack_time=ack_time,
            fills=[f for f in book.fills if f.order_id == order.order_id and f.aggressor],
            fees=fees,
        )
        self._routed_orders.append(routed)
        return routed
