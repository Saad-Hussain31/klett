"""Unit tests for the mock venue."""

import pytest
from simulation.mock_venue import MockVenue
from simulation.order_book import Side, OrderType


class TestMockVenueConnection:
    """Connection state management."""

    def test_starts_disconnected(self):
        v = MockVenue(venue_id=1, name="Test")
        assert v.is_connected is False

    def test_connect(self):
        v = MockVenue(venue_id=1, name="Test")
        v.connect()
        assert v.is_connected is True

    def test_disconnect(self, mock_venue):
        mock_venue.disconnect()
        assert mock_venue.is_connected is False

    def test_reject_when_disconnected(self):
        v = MockVenue(venue_id=1, name="Test")
        report = v.send_order({
            "side": "buy", "quantity": 100, "price": 100.0, "type": "limit"
        })
        assert report["status"] == "rejected"


class TestMockVenueOrders:
    """Order submission and fills."""

    def test_limit_buy_fill(self, mock_venue):
        report = mock_venue.send_order({
            "side": "buy",
            "quantity": 50,
            "price": 100.05,
            "type": "limit",
        })
        assert report["status"] in ("filled", "partially_filled", "accepted")

    def test_market_buy(self, mock_venue):
        report = mock_venue.send_order({
            "side": "buy",
            "quantity": 50,
            "type": "market",
        })
        assert report["status"] in ("filled", "partially_filled", "accepted")

    def test_cancel(self, mock_venue):
        # Send a resting order (price far from market)
        report = mock_venue.send_order({
            "side": "buy",
            "quantity": 50,
            "price": 80.0,
            "type": "limit",
        })
        order_id = report.get("order_id")
        if order_id:
            cancel = mock_venue.cancel_order(order_id)
            assert cancel["status"] in ("canceled", "not_found")

    def test_depth(self, mock_venue):
        depth = mock_venue.get_depth()
        assert "bids" in depth
        assert "asks" in depth

    def test_invalid_side_rejected(self, mock_venue):
        report = mock_venue.send_order({
            "side": "invalid",
            "quantity": 50,
            "type": "market",
        })
        assert report["status"] == "rejected"
