"""Latency analyzer for SOR performance measurement.

Parses structured log files or direct timing data to produce latency
histograms, percentile tables, and stage-by-stage breakdowns.
"""

import csv
import statistics
from dataclasses import dataclass, field
from typing import Optional

import numpy as np


@dataclass
class LatencyBucket:
    """Latency statistics for a single processing stage."""
    stage: str
    samples: list[float] = field(default_factory=list)

    @property
    def count(self) -> int:
        return len(self.samples)

    @property
    def mean(self) -> float:
        return statistics.mean(self.samples) if self.samples else 0.0

    @property
    def median(self) -> float:
        return statistics.median(self.samples) if self.samples else 0.0

    @property
    def stddev(self) -> float:
        return statistics.stdev(self.samples) if len(self.samples) > 1 else 0.0

    @property
    def min_val(self) -> float:
        return min(self.samples) if self.samples else 0.0

    @property
    def max_val(self) -> float:
        return max(self.samples) if self.samples else 0.0

    def percentile(self, p: float) -> float:
        if not self.samples:
            return 0.0
        return float(np.percentile(self.samples, p))

    def summary(self) -> dict:
        return {
            "stage": self.stage,
            "count": self.count,
            "mean_us": round(self.mean, 2),
            "median_us": round(self.median, 2),
            "stddev_us": round(self.stddev, 2),
            "min_us": round(self.min_val, 2),
            "max_us": round(self.max_val, 2),
            "p50_us": round(self.percentile(50), 2),
            "p95_us": round(self.percentile(95), 2),
            "p99_us": round(self.percentile(99), 2),
            "p999_us": round(self.percentile(99.9), 2),
        }


class LatencyAnalyzer:
    """Collects and analyzes latency measurements across pipeline stages.

    Typical stages in the SOR hot path:
        ingestion -> validation -> risk_check -> routing -> venue_send -> ack

    Usage:
        analyzer = LatencyAnalyzer()
        analyzer.record("ingestion", 12.5)
        analyzer.record("routing", 8.3)
        analyzer.print_report()
    """

    def __init__(self):
        self.buckets: dict[str, LatencyBucket] = {}
        self._order_latencies: dict[str, dict[str, float]] = {}

    def record(self, stage: str, latency_us: float):
        """Record a single latency sample for a stage."""
        if stage not in self.buckets:
            self.buckets[stage] = LatencyBucket(stage=stage)
        self.buckets[stage].samples.append(latency_us)

    def record_order(self, order_id: str, stage: str, latency_us: float):
        """Record a latency sample tied to a specific order."""
        self.record(stage, latency_us)
        if order_id not in self._order_latencies:
            self._order_latencies[order_id] = {}
        self._order_latencies[order_id][stage] = latency_us

    def get_end_to_end(self, order_id: str) -> float:
        """Get total end-to-end latency for an order (sum of all stages)."""
        if order_id not in self._order_latencies:
            return 0.0
        return sum(self._order_latencies[order_id].values())

    def load_csv(self, path: str) -> int:
        """Load latency data from a CSV file.

        Expected columns: order_id, stage, latency_us
        Returns number of records loaded.
        """
        count = 0
        with open(path, "r") as f:
            reader = csv.DictReader(f)
            for row in reader:
                order_id = row.get("order_id", "")
                stage = row.get("stage", "unknown")
                latency = float(row.get("latency_us", 0))
                self.record_order(order_id, stage, latency)
                count += 1
        return count

    def generate_synthetic(
        self,
        stages: Optional[list[str]] = None,
        num_orders: int = 10000,
        seed: int = 42,
    ) -> int:
        """Generate synthetic latency data for testing the analyzer."""
        if stages is None:
            stages = ["ingestion", "validation", "risk_check", "routing",
                       "venue_send", "venue_ack"]

        rng = np.random.default_rng(seed)
        # Mean latencies per stage (microseconds)
        stage_params = {
            "ingestion": (5.0, 2.0),
            "validation": (2.0, 0.8),
            "risk_check": (3.0, 1.0),
            "routing": (8.0, 3.0),
            "venue_send": (15.0, 5.0),
            "venue_ack": (45.0, 15.0),
        }

        count = 0
        for i in range(num_orders):
            order_id = f"ORD-{i:06d}"
            for stage in stages:
                mu, sigma = stage_params.get(stage, (10.0, 3.0))
                latency = max(0.1, rng.normal(mu, sigma))
                self.record_order(order_id, stage, latency)
                count += 1

        return count

    def summary(self) -> list[dict]:
        """Return a summary of all stages."""
        return [b.summary() for b in self.buckets.values()]

    def end_to_end_summary(self) -> dict:
        """Return end-to-end latency distribution across all orders."""
        e2e = [self.get_end_to_end(oid) for oid in self._order_latencies]
        if not e2e:
            return {}
        arr = np.array(e2e)
        return {
            "count": len(e2e),
            "mean_us": round(float(np.mean(arr)), 2),
            "median_us": round(float(np.median(arr)), 2),
            "p95_us": round(float(np.percentile(arr, 95)), 2),
            "p99_us": round(float(np.percentile(arr, 99)), 2),
            "p999_us": round(float(np.percentile(arr, 99.9)), 2),
            "min_us": round(float(np.min(arr)), 2),
            "max_us": round(float(np.max(arr)), 2),
        }

    def print_report(self):
        """Print a formatted latency report to stdout."""
        print("=" * 90)
        print("LATENCY ANALYSIS REPORT")
        print("=" * 90)
        print(f"{'Stage':<15} {'Count':>8} {'Mean':>10} {'Median':>10} "
              f"{'P95':>10} {'P99':>10} {'P99.9':>10} {'Max':>10}")
        print("-" * 90)

        for stage, bucket in self.buckets.items():
            s = bucket.summary()
            print(f"{s['stage']:<15} {s['count']:>8} {s['mean_us']:>10.2f} "
                  f"{s['median_us']:>10.2f} {s['p95_us']:>10.2f} "
                  f"{s['p99_us']:>10.2f} {s['p999_us']:>10.2f} "
                  f"{s['max_us']:>10.2f}")

        e2e = self.end_to_end_summary()
        if e2e:
            print("-" * 90)
            print(f"{'END-TO-END':<15} {e2e['count']:>8} {e2e['mean_us']:>10.2f} "
                  f"{e2e['median_us']:>10.2f} {e2e['p95_us']:>10.2f} "
                  f"{e2e['p99_us']:>10.2f} {e2e['p999_us']:>10.2f} "
                  f"{e2e['max_us']:>10.2f}")

        print("=" * 90)
