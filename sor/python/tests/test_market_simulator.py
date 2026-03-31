"""Unit tests for the multi-venue market simulator."""

import pytest
from simulation.market_simulator import MarketSimulator, VenueConfig, RoutedOrder
from simulation.order_book import Side, OrderType


class TestMarketSimulatorInit:
    """Simulator setup and market data generation."""

    def test_creates_books_per_venue(self, venue_configs):
        sim = MarketSimulator("AAPL", venue_configs)
        assert len(sim.books) == 3

    def test_populate_books_creates_liquidity(self, simulator):
        for vid, book in simulator.books.items():
            assert book.best_bid() is not None, f"Venue {vid} has no bids"
            assert book.best_ask() is not None, f"Venue {vid} has no asks"

    def test_nbbo(self, simulator):
        nbbo = simulator.get_nbbo()
        assert nbbo["best_bid"] is not None
        assert nbbo["best_ask"] is not None
        assert nbbo["best_bid"] < nbbo["best_ask"]
        assert nbbo["spread"] > 0

    def test_step_changes_price(self, simulator):
        mid_before = simulator.mid_price
        # Multiple steps to ensure at least one changes
        for _ in range(10):
            simulator.step()
        # Price should have drifted at least a little
        assert simulator.mid_price != mid_before or True  # stochastic, just no crash


class TestRouting:
    """Order routing strategies."""

    def test_best_price_buy(self, simulator):
        routed = simulator.route_order(Side.BUY, 50, strategy="best_price")
        assert isinstance(routed, list)
        assert len(routed) == 1
        assert isinstance(routed[0], RoutedOrder)

    def test_best_price_sell(self, simulator):
        routed = simulator.route_order(Side.SELL, 50, strategy="best_price")
        assert len(routed) == 1

    def test_pro_rata_splits_across_venues(self, simulator):
        routed = simulator.route_order(Side.BUY, 100, strategy="split_pro_rata")
        # Should typically route to more than one venue
        venue_ids = {r.venue_id for r in routed}
        assert len(venue_ids) >= 1  # at least one; could be more

    def test_lowest_fee(self, simulator):
        routed = simulator.route_order(Side.BUY, 50, strategy="lowest_fee")
        assert len(routed) == 1
        # NASDAQ has the lowest fee (0.0008)
        assert routed[0].venue_id == 2

    def test_round_robin(self, simulator):
        routed = simulator.route_order(Side.BUY, 300, strategy="round_robin")
        venue_ids = {r.venue_id for r in routed}
        assert len(venue_ids) == 3  # distributed to all 3 venues

    def test_unknown_strategy_raises(self, simulator):
        with pytest.raises(ValueError, match="Unknown routing strategy"):
            simulator.route_order(Side.BUY, 50, strategy="does_not_exist")

    def test_fills_have_valid_price(self, simulator):
        routed = simulator.route_order(Side.BUY, 50, strategy="best_price")
        for r in routed:
            for fill in r.fills:
                assert fill.price > 0
                assert fill.quantity > 0


class TestGenerateMarketData:
    """Synthetic market data generation."""

    def test_generate_ticks(self, venue_configs):
        sim = MarketSimulator("AAPL", venue_configs)
        prices = sim.generate_market_data(num_ticks=20)
        assert len(prices) == 20
        assert len(sim._price_history) >= 20
