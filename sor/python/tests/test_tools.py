"""Unit tests for the tools package."""

import pytest
from tools.order_generator import generate_orders, generate_vwap_schedule, generate_sweep_order
from tools.fix_message_builder import FixMessageBuilder
from tools.latency_analyzer import LatencyAnalyzer


class TestOrderGenerator:
    """Order generation utilities."""

    def test_generate_orders_count(self):
        orders = generate_orders(num_orders=10)
        assert len(orders) == 10

    def test_generate_orders_fields(self):
        orders = generate_orders(num_orders=5)
        for o in orders:
            assert "side" in o
            assert "quantity" in o
            assert o["quantity"] > 0
            assert o["side"] in ("buy", "sell")

    def test_generate_orders_deterministic(self):
        a = generate_orders(num_orders=5, seed=42)
        b = generate_orders(num_orders=5, seed=42)
        # Timestamps differ due to time.time(); compare without them
        for x, y in zip(a, b):
            assert x["side"] == y["side"]
            assert x["quantity"] == y["quantity"]
            assert x["order_type"] == y["order_type"]

    def test_vwap_schedule(self):
        schedule = generate_vwap_schedule(total_qty=1000, num_slices=10,
                                           volume_profile="uniform")
        assert len(schedule) == 10
        total = sum(s["quantity"] for s in schedule)
        assert total == pytest.approx(1000, abs=1)

    def test_vwap_u_shape(self):
        schedule = generate_vwap_schedule(total_qty=1000, num_slices=10,
                                           volume_profile="u_shape")
        # U-shape: middle slices largest (cos(x-pi/2)^2 peaks at center)
        mid = schedule[4]["quantity"]
        edge = schedule[0]["quantity"]
        assert mid > edge

    def test_sweep_order(self):
        order = generate_sweep_order(total_qty=500, venues=["NYSE", "BATS"])
        assert order["total_quantity"] == 500
        assert order["order_type"] == "ioc"
        assert order["strategy"] == "liquidity_sweep"


class TestFixMessageBuilder:
    """FIX protocol message building and parsing."""

    def test_new_order_single(self):
        builder = FixMessageBuilder(sender_comp_id="SOR",
                                     target_comp_id="VENUE")
        msg = builder.new_order_single(
            cl_ord_id="O001", symbol="AAPL", side="buy",
            quantity=100, price=150.0, order_type="limit"
        )
        assert "35=D" in msg
        assert "55=AAPL" in msg
        assert "11=O001" in msg

    def test_cancel_request(self):
        builder = FixMessageBuilder(sender_comp_id="SOR",
                                     target_comp_id="VENUE")
        msg = builder.cancel_request(
            cl_ord_id="C001", orig_cl_ord_id="O001",
            symbol="AAPL", side="buy", quantity=100
        )
        assert "35=F" in msg

    def test_execution_report(self):
        builder = FixMessageBuilder(sender_comp_id="VENUE",
                                     target_comp_id="SOR")
        msg = builder.execution_report(
            cl_ord_id="O001", exec_id="E001",
            exec_type="fill", ord_status="filled",
            symbol="AAPL", side="buy", order_qty=100,
            last_qty=100, last_px=150.0,
            cum_qty=100, leaves_qty=0, avg_px=150.0,
        )
        assert "35=8" in msg

    def test_roundtrip_parse(self):
        builder = FixMessageBuilder(sender_comp_id="SOR",
                                     target_comp_id="VENUE")
        msg = builder.new_order_single(
            cl_ord_id="O002", symbol="MSFT", side="sell",
            quantity=200, price=300.0, order_type="limit"
        )
        parsed = builder.parse_message(msg)
        assert parsed[55] == "MSFT"  # int keys
        assert parsed[11] == "O002"

    def test_checksum(self):
        builder = FixMessageBuilder(sender_comp_id="SOR",
                                     target_comp_id="VENUE")
        msg = builder.new_order_single(
            cl_ord_id="O003", symbol="GOOG", side="buy",
            quantity=50, price=2800.0, order_type="limit"
        )
        assert "\x0110=" in msg

    def test_heartbeat(self):
        builder = FixMessageBuilder()
        msg = builder.heartbeat()
        assert "35=0" in msg

    def test_logon(self):
        builder = FixMessageBuilder()
        msg = builder.logon()
        assert "35=A" in msg


class TestLatencyAnalyzer:
    """Latency analysis tool."""

    def test_record_and_summary(self):
        analyzer = LatencyAnalyzer()
        analyzer.record("routing", 50.0)
        analyzer.record("routing", 60.0)
        analyzer.record("routing", 70.0)
        summary = analyzer.summary()
        # summary() returns list[dict]; find the routing entry
        routing = [s for s in summary if s["stage"] == "routing"]
        assert len(routing) == 1
        assert routing[0]["mean_us"] == pytest.approx(60.0)
        assert routing[0]["count"] == 3

    def test_generate_synthetic(self):
        analyzer = LatencyAnalyzer()
        count = analyzer.generate_synthetic(num_orders=100)
        assert count > 0
        summary = analyzer.summary()
        assert len(summary) > 0

    def test_percentiles(self):
        analyzer = LatencyAnalyzer()
        for i in range(100):
            analyzer.record("test_stage", float(i))
        summary = analyzer.summary()
        stage = [s for s in summary if s["stage"] == "test_stage"][0]
        assert stage["p50_us"] == pytest.approx(49.5, abs=1)
        assert stage["p99_us"] >= 98

    def test_end_to_end(self):
        analyzer = LatencyAnalyzer()
        analyzer.record_order("O1", "routing", 10.0)
        analyzer.record_order("O1", "venue_send", 20.0)
        assert analyzer.get_end_to_end("O1") == pytest.approx(30.0)

    def test_print_report(self, capsys):
        analyzer = LatencyAnalyzer()
        analyzer.generate_synthetic(num_orders=50)
        analyzer.print_report()
        captured = capsys.readouterr()
        assert "LATENCY" in captured.out
