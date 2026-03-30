"""Mock venue server for testing SOR connectivity.

Provides a simple venue abstraction that accepts order dicts, runs
a matching engine internally, and returns execution reports.
"""

import time
from dataclasses import dataclass, field
from typing import Callable, Optional

from .order_book import OrderBookSimulator, Side, OrderType, OrderStatus, SimOrder


class MockVenue:
    """Simulates a venue that accepts orders via method calls.

    Wraps an OrderBookSimulator and exposes a dict-based order interface
    suitable for integration testing.
    """

    def __init__(self, venue_id: int, name: str, fee_rate: float = 0.001,
                 latency_us: float = 50.0):
        self.venue_id = venue_id
        self.name = name
        self.fee_rate = fee_rate
        self.latency_us = latency_us
        self.book = OrderBookSimulator(f"MOCK-{name}")
        self.orders: dict[int, dict] = {}
        self.execution_callback: Optional[Callable] = None
        self._connected = False

    def connect(self):
        self._connected = True

    def disconnect(self):
        self._connected = False

    @property
    def is_connected(self) -> bool:
        return self._connected

    def set_market_price(self, mid: float, depth: int = 10,
                         base_qty: float = 100.0):
        """Repopulate the book around a new mid price."""
        self.book.bids.clear()
        self.book.asks.clear()
        tick = self.book.tick_size
        for i in range(depth):
            bid_price = round(mid - (i + 1) * tick, 8)
            ask_price = round(mid + (i + 1) * tick, 8)
            qty = base_qty * (1.0 + (depth - i) * 0.1)
            self.book.submit_order(Side.BUY, OrderType.LIMIT, bid_price, qty)
            self.book.submit_order(Side.SELL, OrderType.LIMIT, ask_price, qty)

    def send_order(self, order_dict: dict) -> dict:
        """Accept an order dict and return acknowledgement.

        Expected keys: side ('buy'/'sell'), order_type ('limit'/'market'/'ioc'/'fok'),
        price (float), quantity (float), order_id (optional int).
        """
        if not self._connected:
            return {"status": "rejected", "reason": "venue_disconnected"}

        side_map = {"buy": Side.BUY, "sell": Side.SELL}
        type_map = {
            "limit": OrderType.LIMIT,
            "market": OrderType.MARKET,
            "ioc": OrderType.IOC,
            "fok": OrderType.FOK,
        }

        side = side_map.get(order_dict.get("side", "").lower())
        otype = type_map.get(order_dict.get("order_type", "").lower(), OrderType.LIMIT)
        price = float(order_dict.get("price", 0))
        quantity = float(order_dict.get("quantity", 0))

        if side is None or quantity <= 0:
            return {"status": "rejected", "reason": "invalid_parameters"}

        result = self.book.submit_order(side, otype, price, quantity)
        self.orders[result.order_id] = order_dict

        report = self._build_report(result)

        if self.execution_callback:
            self.execution_callback(report)

        return report

    def cancel_order(self, order_id: int) -> dict:
        """Cancel a resting order."""
        if not self._connected:
            return {"status": "rejected", "reason": "venue_disconnected"}

        ok = self.book.cancel_order(order_id)
        if ok:
            return {
                "status": "canceled",
                "order_id": order_id,
                "venue_id": self.venue_id,
                "timestamp": time.time(),
            }
        return {
            "status": "rejected",
            "order_id": order_id,
            "reason": "order_not_found_or_terminal",
        }

    def process(self):
        """No-op for this mock — matching is synchronous in submit_order."""
        pass

    def get_depth(self, levels: int = 10) -> dict:
        """Return current book depth."""
        return self.book.get_depth(levels)

    def _build_report(self, order: SimOrder) -> dict:
        """Build an execution report dict from a SimOrder."""
        status_map = {
            OrderStatus.ACCEPTED: "accepted",
            OrderStatus.PARTIALLY_FILLED: "partial_fill",
            OrderStatus.FILLED: "filled",
            OrderStatus.CANCELED: "canceled",
            OrderStatus.REJECTED: "rejected",
        }
        # Gather fills for this order
        order_fills = [f for f in self.book.fills if f.order_id == order.order_id]
        last_fill = order_fills[-1] if order_fills else None

        return {
            "status": status_map.get(order.status, "unknown"),
            "order_id": order.order_id,
            "venue_id": self.venue_id,
            "symbol": order.symbol,
            "side": order.side.name.lower(),
            "quantity": order.quantity,
            "filled_qty": order.filled_qty,
            "leaves_qty": order.leaves_qty,
            "avg_price": (
                sum(f.price * f.quantity for f in order_fills) / order.filled_qty
                if order.filled_qty > 0 else 0.0
            ),
            "last_price": last_fill.price if last_fill else 0.0,
            "last_qty": last_fill.quantity if last_fill else 0.0,
            "fee": order.filled_qty * self.fee_rate,
            "timestamp": time.time(),
        }
