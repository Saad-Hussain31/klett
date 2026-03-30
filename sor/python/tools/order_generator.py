"""Order generator for SOR testing.

Generates synthetic order flows with configurable distributions for
side, size, price, and timing. Useful for stress testing and simulation.
"""

import random
import time
from typing import Optional

import numpy as np


def generate_orders(
    num_orders: int = 100,
    symbol: str = "AAPL",
    side_bias: float = 0.5,
    min_qty: float = 10,
    max_qty: float = 1000,
    mid_price: float = 100.0,
    price_spread: float = 0.05,
    order_types: Optional[list[str]] = None,
    seed: int = 42,
) -> list[dict]:
    """Generate a batch of synthetic orders.

    Args:
        num_orders: Number of orders to generate.
        symbol: Instrument symbol.
        side_bias: Probability of a buy order (0.0 = all sells, 1.0 = all buys).
        min_qty: Minimum order quantity.
        max_qty: Maximum order quantity.
        mid_price: Central price around which limit prices cluster.
        price_spread: Max distance from mid as fraction (0.05 = 5%).
        order_types: Allowed order types. Default: ['limit', 'market', 'ioc'].
        seed: Random seed for reproducibility.

    Returns:
        List of order dicts ready for simulation.
    """
    rng = np.random.default_rng(seed)
    if order_types is None:
        order_types = ["limit", "market", "ioc"]

    orders = []
    ts = time.time()

    for i in range(num_orders):
        side = "buy" if rng.random() < side_bias else "sell"
        qty = round(rng.uniform(min_qty, max_qty), 0)
        otype = order_types[rng.integers(0, len(order_types))]

        # Limit price centered on mid with noise
        offset = rng.normal(0, price_spread * mid_price)
        if side == "buy":
            price = round(mid_price - abs(offset), 2)
        else:
            price = round(mid_price + abs(offset), 2)
        price = max(0.01, price)

        order = {
            "order_id": i + 1,
            "symbol": symbol,
            "side": side,
            "order_type": otype,
            "price": price if otype != "market" else None,
            "quantity": qty,
            "timestamp": ts + i * 0.001,
        }
        orders.append(order)

    return orders


def generate_vwap_schedule(
    total_qty: float,
    num_slices: int = 20,
    symbol: str = "AAPL",
    mid_price: float = 100.0,
    duration_sec: float = 300.0,
    volume_profile: str = "uniform",
) -> list[dict]:
    """Generate a time-sliced VWAP order schedule.

    Args:
        total_qty: Total quantity to fill over the schedule.
        num_slices: Number of child orders.
        symbol: Instrument symbol.
        mid_price: Reference price.
        duration_sec: Total duration in seconds.
        volume_profile: 'uniform', 'u_shape', or 'front_loaded'.

    Returns:
        List of child order dicts with target times.
    """
    if volume_profile == "u_shape":
        # More volume at open and close
        x = np.linspace(0, np.pi, num_slices)
        weights = np.cos(x - np.pi / 2) ** 2 + 0.3
    elif volume_profile == "front_loaded":
        weights = np.linspace(2.0, 0.5, num_slices)
    else:
        weights = np.ones(num_slices)

    weights /= weights.sum()
    slice_qtys = np.round(weights * total_qty, 0)
    # Fix rounding drift
    slice_qtys[-1] += total_qty - slice_qtys.sum()

    interval = duration_sec / num_slices
    ts = time.time()

    slices = []
    for i in range(num_slices):
        slices.append({
            "order_id": i + 1,
            "symbol": symbol,
            "side": "buy",
            "order_type": "limit",
            "price": round(mid_price, 2),
            "quantity": float(slice_qtys[i]),
            "target_time": ts + i * interval,
            "slice_index": i,
        })

    return slices


def generate_sweep_order(
    symbol: str = "AAPL",
    side: str = "buy",
    total_qty: float = 5000,
    venues: list[str] = None,
    mid_price: float = 100.0,
) -> dict:
    """Generate a sweep-style order for cross-venue liquidity taking."""
    if venues is None:
        venues = ["NYSE", "NASDAQ", "BATS"]

    return {
        "symbol": symbol,
        "side": side,
        "order_type": "ioc",
        "total_quantity": total_qty,
        "price": round(mid_price * (1.01 if side == "buy" else 0.99), 2),
        "target_venues": venues,
        "strategy": "liquidity_sweep",
        "timestamp": time.time(),
    }
