"""Shared fixtures for SOR Python tests."""

import pytest

from simulation.order_book import OrderBookSimulator, Side, OrderType
from simulation.market_simulator import MarketSimulator, VenueConfig
from simulation.mock_venue import MockVenue


@pytest.fixture
def order_book():
    """Empty order book."""
    return OrderBookSimulator("TEST")


@pytest.fixture
def populated_book():
    """Order book with liquidity on both sides around 100."""
    book = OrderBookSimulator("TEST")
    # Resting sells (asks)
    for i in range(5):
        book.submit_order(Side.SELL, OrderType.LIMIT,
                          price=100.01 + i * 0.01, quantity=100.0)
    # Resting buys (bids)
    for i in range(5):
        book.submit_order(Side.BUY, OrderType.LIMIT,
                          price=99.99 - i * 0.01, quantity=100.0)
    return book


@pytest.fixture
def venue_configs():
    """Three simulated venue configs."""
    return [
        VenueConfig(venue_id=1, name="NYSE", fee_rate=0.001, latency_us=50),
        VenueConfig(venue_id=2, name="NASDAQ", fee_rate=0.0008, latency_us=30),
        VenueConfig(venue_id=3, name="BATS", fee_rate=0.0012, latency_us=45),
    ]


@pytest.fixture
def simulator(venue_configs):
    """Market simulator with populated books."""
    sim = MarketSimulator("AAPL", venue_configs)
    sim.populate_books()
    return sim


@pytest.fixture
def mock_venue():
    """Connected mock venue."""
    v = MockVenue(venue_id=1, name="TestVenue", fee_rate=0.001)
    v.connect()
    v.set_market_price(100.0)
    return v
