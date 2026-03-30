"""Historical data replay engine for backtesting SOR strategies."""

import time
from dataclasses import dataclass, field
from typing import Callable, Optional

import numpy as np


@dataclass
class Tick:
    """A single market data tick."""
    timestamp: float
    symbol: str
    venue: str
    bid: float
    bid_qty: float
    ask: float
    ask_qty: float


class ReplayEngine:
    """Replays historical or synthetic market data for backtesting.

    Supports loading from CSV, generating synthetic data, and replaying
    tick-by-tick with configurable speed.
    """

    def __init__(self):
        self.ticks: list[Tick] = []
        self.position: int = 0
        self.callbacks: list[Callable] = []
        self._running = False

    def load_csv(self, path: str) -> int:
        """Load ticks from a CSV file.

        Expected columns: timestamp, symbol, venue, bid, bid_qty, ask, ask_qty
        Returns number of ticks loaded.
        """
        self.ticks = []
        with open(path, "r") as f:
            header = f.readline().strip().split(",")
            for line in f:
                parts = line.strip().split(",")
                if len(parts) < 7:
                    continue
                self.ticks.append(Tick(
                    timestamp=float(parts[0]),
                    symbol=parts[1].strip(),
                    venue=parts[2].strip(),
                    bid=float(parts[3]),
                    bid_qty=float(parts[4]),
                    ask=float(parts[5]),
                    ask_qty=float(parts[6]),
                ))
        self.position = 0
        return len(self.ticks)

    def generate_synthetic(
        self,
        symbol: str,
        venues: list[str],
        num_ticks: int = 10000,
        start_price: float = 100.0,
        volatility: float = 0.001,
        tick_interval: float = 0.001,
    ) -> int:
        """Generate synthetic tick data using geometric Brownian motion.

        Args:
            symbol: Instrument symbol.
            venues: List of venue names.
            num_ticks: Total number of ticks to generate.
            start_price: Initial mid price.
            volatility: Per-tick volatility.
            tick_interval: Seconds between ticks.

        Returns:
            Number of ticks generated.
        """
        rng = np.random.default_rng(42)
        self.ticks = []
        mid = start_price
        ts = time.time()

        for i in range(num_ticks):
            # GBM step
            mid *= np.exp(volatility * rng.standard_normal())
            ts += tick_interval

            for venue in venues:
                # Per-venue noise
                venue_noise = rng.uniform(-0.0001, 0.0001) * mid
                venue_mid = mid + venue_noise
                half_spread = max(0.01, abs(rng.normal(0.02, 0.005)))

                bid = round(venue_mid - half_spread, 4)
                ask = round(venue_mid + half_spread, 4)
                bid_qty = round(rng.uniform(50, 500), 0)
                ask_qty = round(rng.uniform(50, 500), 0)

                self.ticks.append(Tick(
                    timestamp=ts,
                    symbol=symbol,
                    venue=venue,
                    bid=bid,
                    bid_qty=bid_qty,
                    ask=ask,
                    ask_qty=ask_qty,
                ))

        # Sort by timestamp
        self.ticks.sort(key=lambda t: t.timestamp)
        self.position = 0
        return len(self.ticks)

    def on_tick(self, callback: Callable[[Tick], None]):
        """Register a tick callback."""
        self.callbacks.append(callback)

    def replay(self, callback: Optional[Callable] = None, speed: float = 1.0):
        """Replay all ticks with timing.

        Args:
            callback: Optional callback invoked for each tick.
            speed: Speed multiplier (2.0 = twice as fast). 0 = no delay.
        """
        if callback:
            self.callbacks.append(callback)

        self._running = True
        self.position = 0

        while self.position < len(self.ticks) and self._running:
            tick = self.ticks[self.position]

            for cb in self.callbacks:
                cb(tick)

            self.position += 1

            # Timing
            if speed > 0 and self.position < len(self.ticks):
                next_tick = self.ticks[self.position]
                delay = (next_tick.timestamp - tick.timestamp) / speed
                if delay > 0:
                    time.sleep(delay)

        self._running = False

    def step(self) -> Optional[Tick]:
        """Advance one tick and return it, or None if exhausted."""
        if self.position >= len(self.ticks):
            return None

        tick = self.ticks[self.position]
        for cb in self.callbacks:
            cb(tick)
        self.position += 1
        return tick

    def stop(self):
        """Stop an ongoing replay."""
        self._running = False

    def reset(self):
        """Reset replay position to the beginning."""
        self.position = 0

    @property
    def total_ticks(self) -> int:
        return len(self.ticks)

    @property
    def progress(self) -> float:
        """Replay progress as a fraction (0.0 to 1.0)."""
        if not self.ticks:
            return 0.0
        return self.position / len(self.ticks)
