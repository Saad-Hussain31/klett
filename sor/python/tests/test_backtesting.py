"""Tests for backtesting: replay engine, strategy evaluator, scenario runner."""

import pytest
from backtesting.replay_engine import ReplayEngine, Tick
from backtesting.strategy_evaluator import StrategyEvaluator
from backtesting.scenario_runner import (
    ScenarioRunner, basic_buy_scenario, high_volume_sweep_scenario,
    volatile_market_scenario,
)
from simulation.market_simulator import MarketSimulator, VenueConfig


@pytest.fixture
def replay_engine():
    return ReplayEngine()


@pytest.fixture
def evaluator(simulator):
    return StrategyEvaluator(simulator)


class TestReplayEngine:
    """Replay engine tests."""

    def test_generate_synthetic(self, replay_engine):
        count = replay_engine.generate_synthetic(
            symbol="AAPL", num_ticks=100, start_price=150.0,
            venues=["NYSE", "NASDAQ"]
        )
        # 100 ticks * 2 venues = 200 total ticks
        assert replay_engine.total_ticks == 200
        assert count == 200

    def test_step(self, replay_engine):
        replay_engine.generate_synthetic(
            symbol="AAPL", num_ticks=50, start_price=150.0,
            venues=["NYSE"]
        )
        tick = replay_engine.step()
        assert isinstance(tick, Tick)
        assert tick.symbol == "AAPL"
        assert tick.bid > 0
        assert tick.ask > 0
        assert tick.ask >= tick.bid

    def test_on_tick_callback(self, replay_engine):
        replay_engine.generate_synthetic(
            symbol="AAPL", num_ticks=10, start_price=150.0,
            venues=["NYSE"]
        )
        ticks_received = []
        replay_engine.on_tick(lambda t: ticks_received.append(t))

        while replay_engine.progress < 1.0:
            replay_engine.step()

        assert len(ticks_received) == 10

    def test_reset(self, replay_engine):
        replay_engine.generate_synthetic(
            symbol="AAPL", num_ticks=10, start_price=150.0,
            venues=["NYSE"]
        )
        replay_engine.step()
        replay_engine.step()
        replay_engine.reset()
        assert replay_engine.progress == 0.0


class TestStrategyEvaluator:
    """Strategy evaluation tests."""

    def test_evaluate_best_price(self, evaluator):
        orders = [
            {"side": "buy", "quantity": 50},
            {"side": "sell", "quantity": 50},
        ]
        result = evaluator.evaluate_strategy("best_price", orders,
                                              num_simulations=5)
        assert result.strategy_name == "best_price"
        assert result.total_orders == 10  # 2 orders * 5 sims
        assert result.fill_rate > 0

    def test_compare_strategies(self, evaluator):
        orders = [{"side": "buy", "quantity": 50}]
        results = evaluator.compare_strategies(
            ["best_price", "lowest_fee"], orders, num_simulations=3
        )
        assert "best_price" in results
        assert "lowest_fee" in results

    def test_generate_report(self, evaluator):
        orders = [{"side": "buy", "quantity": 50}]
        evaluator.evaluate_strategy("best_price", orders, num_simulations=3)
        report = evaluator.generate_report()
        assert "best_price" in report
        assert "VWAP" in report


class TestScenarioRunner:
    """Scenario-based testing."""

    def test_basic_buy_scenario(self):
        scenario = basic_buy_scenario()
        runner = ScenarioRunner()
        result = runner.run(scenario)
        assert result.scenario_name == "basic_buy"
        assert result.total_orders == 10

    def test_high_volume_sweep(self):
        scenario = high_volume_sweep_scenario()
        runner = ScenarioRunner()
        result = runner.run(scenario)
        assert result.total_orders == 1

    def test_volatile_market(self):
        scenario = volatile_market_scenario()
        runner = ScenarioRunner()
        result = runner.run(scenario)
        assert result.total_orders == 20

    def test_run_all(self):
        runner = ScenarioRunner()
        runner.add(basic_buy_scenario())
        runner.add(high_volume_sweep_scenario())
        results = runner.run_all()
        assert len(results) == 2

    def test_print_report(self, capsys):
        runner = ScenarioRunner()
        runner.add(basic_buy_scenario())
        results = runner.run_all()
        ScenarioRunner.print_report(results)
        captured = capsys.readouterr()
        assert "SCENARIO" in captured.out
