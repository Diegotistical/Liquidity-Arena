"""
latency_model.py — Configurable per-agent latency simulation.

In real markets, latency determines who gets filled:
  - Co-located HFT: ~1-10µs
  - Market makers: ~50-200µs
  - Retail/noise: ~1-10ms

Latency affects:
  1. When an agent's order reaches the exchange (stale quotes → adverse selection)
  2. Whether a cancel arrives before a fill (race condition)
  3. Latency arb profitability (faster agents pick off slower ones)

This model adds a configurable delay to each agent's actions,
making the simulation realistic enough that latency-sensitive
strategies actually matter.
"""

import numpy as np
from dataclasses import dataclass


@dataclass
class LatencyConfig:
    """Latency configuration in microseconds."""
    base_us: float = 200.0      # Base latency (µs)
    jitter_us: float = 50.0     # Random jitter (µs, uniform)
    spike_prob: float = 0.001   # Probability of latency spike
    spike_factor: float = 10.0  # Spike multiplier


class LatencyModel:
    """
    Simulates network + processing latency for an agent.

    Each call to sample() returns a delay in simulation time units
    (normalized to step fractions). The delay includes:
      - Base latency (deterministic component)
      - Jitter (uniform random noise)
      - Occasional spikes (modeling GC pauses, network congestion)
    """

    def __init__(self, config: LatencyConfig = None, seed: int = 42):
        self.config = config or LatencyConfig()
        self.rng = np.random.default_rng(seed)

    def sample(self) -> float:
        """
        Sample a latency value in microseconds.

        Returns:
            Latency in microseconds (µs).
        """
        base = self.config.base_us
        jitter = self.rng.uniform(-self.config.jitter_us, self.config.jitter_us)
        delay = base + jitter

        # Occasional spike (GC pause, network congestion).
        if self.rng.random() < self.config.spike_prob:
            delay *= self.config.spike_factor

        return max(0.0, delay)

    def sample_seconds(self) -> float:
        """Sample latency in seconds (for use with event timestamps)."""
        return self.sample() / 1_000_000.0


# ── Pre-configured latency profiles ──────────────────────────────────

def colocated_latency(seed: int = 42) -> LatencyModel:
    """Co-located HFT: ~5µs base, minimal jitter."""
    return LatencyModel(LatencyConfig(base_us=5.0, jitter_us=2.0), seed)

def market_maker_latency(seed: int = 42) -> LatencyModel:
    """Market maker: ~200µs base, moderate jitter."""
    return LatencyModel(LatencyConfig(base_us=200.0, jitter_us=50.0), seed)

def retail_latency(seed: int = 42) -> LatencyModel:
    """Retail/noise trader: ~1ms base, high jitter."""
    return LatencyModel(LatencyConfig(base_us=1000.0, jitter_us=300.0), seed)

def latency_arb_latency(seed: int = 42) -> LatencyModel:
    """Latency arbitrageur: ~50µs base, low jitter."""
    return LatencyModel(LatencyConfig(base_us=50.0, jitter_us=10.0), seed)
