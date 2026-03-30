"""Simulated order book with price-time priority matching engine.

Provides a full limit order book implementation supporting LIMIT, MARKET,
IOC (Immediate-or-Cancel), and FOK (Fill-or-Kill) order types. Orders are
matched using strict price-time priority.
"""

import time
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Optional


class Side(Enum):
    """Order side."""
    BUY = auto()
    SELL = auto()


class OrderType(Enum):
    """Supported order types."""
    LIMIT = auto()
    MARKET = auto()
    IOC = auto()   # Immediate-or-Cancel: fill what you can, cancel rest
    FOK = auto()   # Fill-or-Kill: fill entirely or cancel entirely


class OrderStatus(Enum):
    """Order lifecycle status."""
    NEW = auto()
    ACCEPTED = auto()
    PARTIALLY_FILLED = auto()
    FILLED = auto()
    CANCELED = auto()
    REJECTED = auto()


@dataclass
class SimOrder:
    """Represents a single order in the simulation."""
    order_id: int
    symbol: str
    side: Side
    order_type: OrderType
    price: float
    quantity: float
    filled_qty: float = 0.0
    status: OrderStatus = OrderStatus.NEW
    timestamp: float = field(default_factory=time.time)

    @property
    def leaves_qty(self) -> float:
        """Remaining unfilled quantity."""
        return self.quantity - self.filled_qty


@dataclass
class Fill:
    """Represents an execution / fill event."""
    order_id: int
    price: float
    quantity: float
    side: Side
    timestamp: float
    aggressor: bool  # True if this order was the taker


@dataclass
class PriceLevel:
    """A single price level in the order book, holding a FIFO queue of orders."""
    price: float
    orders: list = field(default_factory=list)
    # Each entry in orders is (timestamp, order_id, quantity_remaining)

    @property
    def total_qty(self) -> float:
        """Total resting quantity at this price level."""
        return sum(qty for _, _, qty in self.orders)


class OrderBookSimulator:
    """Full limit order book with price-time priority matching.

    Supports LIMIT, MARKET, IOC, and FOK order types. Resting orders sit
    in bid/ask maps keyed by price; within each price level orders are
    matched in FIFO (time-priority) order.
    """

    def __init__(self, symbol: str, tick_size: float = 0.01):
        self.symbol = symbol
        self.tick_size = tick_size
        self.bids: dict[float, PriceLevel] = {}   # price -> PriceLevel
        self.asks: dict[float, PriceLevel] = {}   # price -> PriceLevel
        self.orders: dict[int, SimOrder] = {}      # order_id -> SimOrder
        self.fills: list[Fill] = []
        self._next_id = 1

    # ------------------------------------------------------------------
    # Public API
    # ------------------------------------------------------------------

    def submit_order(
        self,
        side: Side,
        order_type: OrderType,
        price: float,
        quantity: float,
    ) -> SimOrder:
        """Submit an order to the book.

        The order is first matched against resting liquidity on the opposite
        side.  Any unfilled residual is handled according to the order type:
        - LIMIT: resting residual is added to the book.
        - MARKET: residual is canceled (no resting market orders).
        - IOC: residual is canceled.
        - FOK: if the full quantity cannot be filled immediately the order is
          rejected with no partial fills.

        Returns the SimOrder with updated status and fill information.
        """
        order = SimOrder(
            order_id=self._next_id,
            symbol=self.symbol,
            side=side,
            order_type=order_type,
            price=price,
            quantity=quantity,
        )
        self._next_id += 1
        self.orders[order.order_id] = order
        order.status = OrderStatus.ACCEPTED

        # For FOK, check if full fill is possible before matching
        if order_type == OrderType.FOK:
            available = self._available_liquidity(order)
            if available < quantity:
                order.status = OrderStatus.REJECTED
                return order

        # Attempt matching
        new_fills = self._match(order)
        self.fills.extend(new_fills)

        # Handle residual
        if order.leaves_qty > 0:
            if order_type == OrderType.LIMIT:
                self._add_to_book(order)
            else:
                # MARKET / IOC / FOK residual -> cancel
                if order.filled_qty > 0:
                    order.status = OrderStatus.PARTIALLY_FILLED
                else:
                    order.status = OrderStatus.CANCELED

        return order

    def cancel_order(self, order_id: int) -> bool:
        """Cancel an existing resting order.

        Returns True if the order was found and canceled, False otherwise.
        """
        if order_id not in self.orders:
            return False

        order = self.orders[order_id]
        if order.status in (OrderStatus.FILLED, OrderStatus.CANCELED, OrderStatus.REJECTED):
            return False

        self._remove_from_book(order_id)
        order.status = OrderStatus.CANCELED
        return True

    def best_bid(self) -> Optional[float]:
        """Return the highest bid price, or None if no bids."""
        if not self.bids:
            return None
        return max(self.bids.keys())

    def best_ask(self) -> Optional[float]:
        """Return the lowest ask price, or None if no asks."""
        if not self.asks:
            return None
        return min(self.asks.keys())

    def mid_price(self) -> Optional[float]:
        """Return the mid-point between best bid and best ask."""
        bb = self.best_bid()
        ba = self.best_ask()
        if bb is None or ba is None:
            return None
        return round((bb + ba) / 2.0, 8)

    def spread(self) -> Optional[float]:
        """Return the spread (best_ask - best_bid)."""
        bb = self.best_bid()
        ba = self.best_ask()
        if bb is None or ba is None:
            return None
        return round(ba - bb, 8)

    def get_depth(self, levels: int = 10) -> dict:
        """Return the top *levels* price levels on each side.

        Returns a dict with keys ``"bids"`` and ``"asks"``, each containing
        a list of ``(price, total_qty)`` tuples sorted best-to-worst.
        """
        sorted_bids = sorted(self.bids.keys(), reverse=True)[:levels]
        sorted_asks = sorted(self.asks.keys())[:levels]

        bid_depth = [
            (p, self.bids[p].total_qty)
            for p in sorted_bids
            if self.bids[p].total_qty > 0
        ]
        ask_depth = [
            (p, self.asks[p].total_qty)
            for p in sorted_asks
            if self.asks[p].total_qty > 0
        ]
        return {"bids": bid_depth, "asks": ask_depth}

    # ------------------------------------------------------------------
    # Internal matching logic
    # ------------------------------------------------------------------

    def _available_liquidity(self, order: SimOrder) -> float:
        """Check how much liquidity is available for an order without executing."""
        if order.side == Side.BUY:
            book_side = self.asks
            prices = sorted(book_side.keys())
            price_ok = lambda p: order.order_type == OrderType.MARKET or p <= order.price
        else:
            book_side = self.bids
            prices = sorted(book_side.keys(), reverse=True)
            price_ok = lambda p: order.order_type == OrderType.MARKET or p >= order.price

        total = 0.0
        for price in prices:
            if not price_ok(price):
                break
            level = book_side[price]
            for _, _, qty in level.orders:
                total += qty
                if total >= order.quantity:
                    return total
        return total

    def _match(self, order: SimOrder) -> list[Fill]:
        """Try to match an incoming order against resting orders.

        Walks through the opposite side of the book in price-time priority,
        generating fills until the order is fully filled or no more
        matchable liquidity exists.
        """
        fills: list[Fill] = []

        if order.side == Side.BUY:
            book_side = self.asks
            prices = sorted(book_side.keys())
            price_ok = lambda p: order.order_type == OrderType.MARKET or p <= order.price
        else:
            book_side = self.bids
            prices = sorted(book_side.keys(), reverse=True)
            price_ok = lambda p: order.order_type == OrderType.MARKET or p >= order.price

        empty_levels: list[float] = []

        for price in prices:
            if order.leaves_qty <= 0:
                break
            if not price_ok(price):
                break

            level = book_side[price]
            remaining_orders: list[tuple] = []

            for ts, resting_id, resting_qty in level.orders:
                if order.leaves_qty <= 0:
                    remaining_orders.append((ts, resting_id, resting_qty))
                    continue

                fill_qty = min(order.leaves_qty, resting_qty)
                fill_ts = time.time()

                # Fill for the aggressor (incoming order)
                fills.append(Fill(
                    order_id=order.order_id,
                    price=price,
                    quantity=fill_qty,
                    side=order.side,
                    timestamp=fill_ts,
                    aggressor=True,
                ))
                # Fill for the resting order
                fills.append(Fill(
                    order_id=resting_id,
                    price=price,
                    quantity=fill_qty,
                    side=Side.SELL if order.side == Side.BUY else Side.BUY,
                    timestamp=fill_ts,
                    aggressor=False,
                ))

                # Update quantities
                order.filled_qty += fill_qty
                leftover = resting_qty - fill_qty

                # Update resting order object
                if resting_id in self.orders:
                    resting_order = self.orders[resting_id]
                    resting_order.filled_qty += fill_qty
                    if resting_order.leaves_qty <= 0:
                        resting_order.status = OrderStatus.FILLED
                    else:
                        resting_order.status = OrderStatus.PARTIALLY_FILLED

                if leftover > 0:
                    remaining_orders.append((ts, resting_id, leftover))

            level.orders = remaining_orders
            if not level.orders:
                empty_levels.append(price)

        # Clean up empty levels
        for price in empty_levels:
            del book_side[price]

        # Update aggressor order status
        if order.leaves_qty <= 0:
            order.status = OrderStatus.FILLED
        elif order.filled_qty > 0:
            order.status = OrderStatus.PARTIALLY_FILLED

        return fills

    def _add_to_book(self, order: SimOrder):
        """Add a resting order to the appropriate side of the book."""
        if order.side == Side.BUY:
            book_side = self.bids
        else:
            book_side = self.asks

        price = order.price
        if price not in book_side:
            book_side[price] = PriceLevel(price=price)

        book_side[price].orders.append(
            (order.timestamp, order.order_id, order.leaves_qty)
        )

    def _remove_from_book(self, order_id: int):
        """Remove a specific order from the book (used by cancel)."""
        order = self.orders.get(order_id)
        if order is None:
            return

        if order.side == Side.BUY:
            book_side = self.bids
        else:
            book_side = self.asks

        price = order.price
        if price not in book_side:
            return

        level = book_side[price]
        level.orders = [
            (ts, oid, qty)
            for ts, oid, qty in level.orders
            if oid != order_id
        ]
        if not level.orders:
            del book_side[price]
