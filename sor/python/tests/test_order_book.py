"""Unit tests for the order book matching engine."""

import pytest
from simulation.order_book import (
    OrderBookSimulator, Side, OrderType, OrderStatus,
)


class TestOrderBookBasics:
    """Basic order book operations."""

    def test_empty_book(self, order_book):
        assert order_book.best_bid() is None
        assert order_book.best_ask() is None
        assert order_book.mid_price() is None
        assert order_book.spread() is None

    def test_limit_buy_rests(self, order_book):
        order = order_book.submit_order(Side.BUY, OrderType.LIMIT,
                                         price=100.0, quantity=50)
        assert order_book.best_bid() == 100.0
        assert order_book.best_ask() is None

    def test_limit_sell_rests(self, order_book):
        order = order_book.submit_order(Side.SELL, OrderType.LIMIT,
                                         price=100.0, quantity=50)
        assert order_book.best_ask() == 100.0
        assert order_book.best_bid() is None

    def test_spread(self, populated_book):
        bid = populated_book.best_bid()
        ask = populated_book.best_ask()
        assert bid is not None
        assert ask is not None
        assert ask > bid
        spread = populated_book.spread()
        assert spread == pytest.approx(ask - bid)

    def test_mid_price(self, populated_book):
        mid = populated_book.mid_price()
        assert mid is not None
        assert mid == pytest.approx(
            (populated_book.best_bid() + populated_book.best_ask()) / 2
        )


class TestMatching:
    """Order matching / fill generation."""

    def test_market_buy_fills(self, populated_book):
        order = populated_book.submit_order(
            Side.BUY, OrderType.MARKET, price=0, quantity=50
        )
        assert order.filled_qty > 0
        assert order.filled_qty == 50.0

    def test_market_sell_fills(self, populated_book):
        order = populated_book.submit_order(
            Side.SELL, OrderType.MARKET, price=0, quantity=50
        )
        assert order.filled_qty > 0
        assert order.filled_qty == 50.0

    def test_limit_buy_crosses_spread(self, populated_book):
        """Limit buy at ask price should fill immediately."""
        ask = populated_book.best_ask()
        order = populated_book.submit_order(
            Side.BUY, OrderType.LIMIT, price=ask, quantity=50
        )
        assert order.filled_qty == 50.0

    def test_limit_sell_crosses_spread(self, populated_book):
        """Limit sell at bid price should fill immediately."""
        bid = populated_book.best_bid()
        order = populated_book.submit_order(
            Side.SELL, OrderType.LIMIT, price=bid, quantity=50
        )
        assert order.filled_qty == 50.0

    def test_partial_fill(self, populated_book):
        """Order larger than available at best price."""
        ask = populated_book.best_ask()
        order = populated_book.submit_order(
            Side.BUY, OrderType.LIMIT, price=ask, quantity=150
        )
        # Best level had 100 qty, so 100 matched, 50 rests
        assert order.filled_qty == 100.0
        assert order.status == OrderStatus.PARTIALLY_FILLED

    def test_ioc_cancels_residual(self, populated_book):
        """IOC order: fill what you can, cancel the rest."""
        ask = populated_book.best_ask()
        order = populated_book.submit_order(
            Side.BUY, OrderType.IOC, price=ask, quantity=500
        )
        assert order.filled_qty > 0
        assert order.filled_qty <= 500
        # IOC doesn't rest; leftover is cancelled or partially filled
        assert order.status in (
            OrderStatus.FILLED, OrderStatus.CANCELED,
            OrderStatus.PARTIALLY_FILLED,
        )

    def test_fok_rejects_insufficient(self, order_book):
        """FOK with no liquidity should reject."""
        order = order_book.submit_order(
            Side.BUY, OrderType.FOK, price=100, quantity=500
        )
        assert order.status == OrderStatus.REJECTED
        assert order.filled_qty == 0

    def test_cancel_order(self, populated_book):
        # Place a resting order far from market
        order = populated_book.submit_order(
            Side.BUY, OrderType.LIMIT, price=90.0, quantity=100
        )
        assert populated_book.cancel_order(order.order_id) is True
        # Can't cancel same order twice
        assert populated_book.cancel_order(order.order_id) is False


class TestDepth:
    """Order book depth queries."""

    def test_get_depth(self, populated_book):
        depth = populated_book.get_depth(3)
        assert "bids" in depth
        assert "asks" in depth
        assert len(depth["bids"]) <= 3
        assert len(depth["asks"]) <= 3
        # Bids descending
        for i in range(len(depth["bids"]) - 1):
            assert depth["bids"][i][0] >= depth["bids"][i + 1][0]
        # Asks ascending
        for i in range(len(depth["asks"]) - 1):
            assert depth["asks"][i][0] <= depth["asks"][i + 1][0]

    def test_price_priority(self, order_book):
        """Orders at better prices match first."""
        order_book.submit_order(Side.SELL, OrderType.LIMIT,
                                price=100.05, quantity=50)
        order_book.submit_order(Side.SELL, OrderType.LIMIT,
                                price=100.10, quantity=50)
        order = order_book.submit_order(Side.BUY, OrderType.MARKET,
                                         price=0, quantity=50)
        assert order.filled_qty == 50
        # Check fills went at lower price
        fills = [f for f in order_book.fills if f.order_id == order.order_id]
        assert fills[0].price == 100.05
