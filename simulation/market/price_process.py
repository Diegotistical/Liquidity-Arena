"""
price_process.py — Synthetic price generators and order flow processes.

Provides the "fundamental value" that informed traders observe.
The market maker does NOT see this — the information asymmetry is
the key microstructure dynamic driving adverse selection.

Supported price processes:
  - GBM (Geometric Brownian Motion): dS = μ·S·dt + σ·S·dW
  - OU (Ornstein-Uhlenbeck): dS = κ(θ − S)·dt + σ·dW
  - Regime-Switching: Markov-modulated volatility across {calm, volatile, news}

Supported order flow processes:
  - Hawkes process: self-exciting point process with order flow clustering

Why this matters:
  - Real markets don't have constant volatility
  - Order flow is NOT Poisson — it clusters (Hawkes captures this)
  - Regime switches model news events, market opens, etc.
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass
from enum import IntEnum

import numpy as np

# ═══════════════════════════════════════════════════════════════════════
# Price Processes
# ═══════════════════════════════════════════════════════════════════════


class PriceProcess(ABC):
    """Abstract base class for price processes."""

    @abstractmethod
    def generate(self, num_steps: int) -> np.ndarray:
        """Generate a price path (in integer ticks)."""

    @abstractmethod
    def step(self) -> int:
        """Generate the next price tick (online mode)."""


class GBMProcess(PriceProcess):
    """
    Geometric Brownian Motion.
    dS = μ·S·dt + σ·S·dW (Euler-Maruyama discretization)
    """

    def __init__(
        self,
        initial_price: int = 10000,
        mu: float = 0.0,
        sigma: float = 0.005,
        dt: float = 0.001,
        seed: int = 42,
    ):
        self.initial_price = initial_price
        self.mu = mu
        self.sigma = sigma
        self.dt = dt
        self.rng = np.random.default_rng(seed)
        self.current_price = float(initial_price)

    def generate(self, num_steps: int) -> np.ndarray:
        """Generate full path upfront."""
        prices = np.zeros(num_steps, dtype=np.int64)
        s = float(self.initial_price)
        for i in range(num_steps):
            dw = self.rng.normal(0, np.sqrt(self.dt))
            s *= 1 + self.mu * self.dt + self.sigma * dw
            prices[i] = int(round(s))
        return prices

    def step(self) -> int:
        dw = self.rng.normal(0, np.sqrt(self.dt))
        self.current_price *= 1 + self.mu * self.dt + self.sigma * dw
        return int(round(self.current_price))


class OUProcess(PriceProcess):
    """
    Ornstein-Uhlenbeck (mean-reverting) process.
    dS = κ(θ − S)·dt + σ·dW

    More realistic for tick-level simulation: prices mean-revert
    over short horizons, creating a stationary spread.
    """

    def __init__(
        self,
        initial_price: int = 10000,
        kappa: float = 0.5,
        theta: int = 10000,
        sigma: float = 50.0,
        dt: float = 0.001,
        seed: int = 42,
    ):
        self.initial_price = initial_price
        self.kappa = kappa
        self.theta = float(theta)
        self.sigma = sigma
        self.dt = dt
        self.rng = np.random.default_rng(seed)
        self.current_price = float(initial_price)

    def generate(self, num_steps: int) -> np.ndarray:
        prices = np.zeros(num_steps, dtype=np.int64)
        s = float(self.initial_price)
        for i in range(num_steps):
            dw = self.rng.normal(0, np.sqrt(self.dt))
            s += self.kappa * (self.theta - s) * self.dt + self.sigma * dw
            prices[i] = int(round(s))
        return prices

    def step(self) -> int:
        dw = self.rng.normal(0, np.sqrt(self.dt))
        self.current_price += (
            self.kappa * (self.theta - self.current_price) * self.dt + self.sigma * dw
        )
        return int(round(self.current_price))


# ═══════════════════════════════════════════════════════════════════════
# Regime-Switching Price Process
# ═══════════════════════════════════════════════════════════════════════


class Regime(IntEnum):
    CALM = 0
    VOLATILE = 1
    NEWS = 2


@dataclass
class RegimeParams:
    """Parameters for a single volatility regime."""

    sigma: float  # Volatility in this regime
    duration_mean: int  # Average duration (steps) before switching
    jump_size: float = 0.0  # Jump magnitude on entry (for NEWS regime)


class RegimeSwitchingProcess(PriceProcess):
    """
    Markov-modulated price process with 3 regimes.

    The process switches between:
      - CALM:     Low volatility, long duration
      - VOLATILE: High volatility, moderate duration
      - NEWS:     Very high volatility + price jump, short duration

    Transition matrix:
                    CALM    VOLATILE  NEWS
      CALM      [  0.95    0.04      0.01  ]
      VOLATILE  [  0.10    0.85      0.05  ]
      NEWS      [  0.30    0.50      0.20  ]

    This models real market dynamics: most of the time is calm,
    with periodic bursts of volatility and rare news events.
    """

    DEFAULT_REGIMES = {
        Regime.CALM: RegimeParams(sigma=30.0, duration_mean=200),
        Regime.VOLATILE: RegimeParams(sigma=100.0, duration_mean=50),
        Regime.NEWS: RegimeParams(sigma=200.0, duration_mean=10, jump_size=50.0),
    }

    # Transition probability matrix (row = from, col = to)
    TRANSITION_MATRIX = np.array(
        [
            [0.95, 0.04, 0.01],  # From CALM
            [0.10, 0.85, 0.05],  # From VOLATILE
            [0.30, 0.50, 0.20],  # From NEWS
        ]
    )

    def __init__(
        self,
        initial_price: int = 10000,
        kappa: float = 0.3,
        theta: int = 10000,
        regimes: dict | None = None,
        dt: float = 0.001,
        seed: int = 42,
    ):
        self.initial_price = initial_price
        self.kappa = kappa
        self.theta = float(theta)
        self.dt = dt
        self.rng = np.random.default_rng(seed)
        self.current_price = float(initial_price)

        self.regimes = regimes or self.DEFAULT_REGIMES
        self.current_regime = Regime.CALM
        self._steps_in_regime = 0

    def _maybe_switch_regime(self):
        """Check if we should transition to a new regime."""
        self._steps_in_regime += 1
        duration = self.regimes[self.current_regime].duration_mean
        # Switch probability increases with time in regime.
        switch_prob = 1.0 - np.exp(-self._steps_in_regime / max(duration, 1))
        if self.rng.random() < switch_prob * 0.1:
            # Sample next regime from transition matrix.
            probs = self.TRANSITION_MATRIX[int(self.current_regime)]
            new_regime = Regime(self.rng.choice(3, p=probs))
            if new_regime != self.current_regime:
                self.current_regime = new_regime
                self._steps_in_regime = 0
                # Apply jump on NEWS entry.
                if new_regime == Regime.NEWS:
                    jump = self.regimes[Regime.NEWS].jump_size
                    direction = self.rng.choice([-1, 1])
                    self.current_price += direction * jump

    def step(self) -> int:
        self._maybe_switch_regime()
        sigma = self.regimes[self.current_regime].sigma
        dw = self.rng.normal(0, np.sqrt(self.dt))
        self.current_price += self.kappa * (self.theta - self.current_price) * self.dt + sigma * dw
        return int(round(self.current_price))

    def generate(self, num_steps: int) -> np.ndarray:
        prices = np.zeros(num_steps, dtype=np.int64)
        for i in range(num_steps):
            prices[i] = self.step()
        return prices


# ═══════════════════════════════════════════════════════════════════════
# Hawkes Process (Order Flow Clustering)
# ═══════════════════════════════════════════════════════════════════════


class HawkesProcess:
    """
    Hawkes self-exciting point process for realistic order flow.

    Intensity: λ(t) = μ + Σ α·exp(-β·(t - tᵢ))

    Where:
      μ = base intensity (background rate)
      α = excitation amplitude (how much each event increases future intensity)
      β = decay rate (how quickly excitement dies down)

    Properties:
      - α/β < 1 ensures stationarity (required)
      - Higher α → more clustering (burst-like behavior)
      - Lower β → longer memory (events influence further into future)

    In real markets:
      - Order flow is NOT Poisson — it clusters
      - One aggressive order triggers more aggressive orders (momentum)
      - This creates realistic patterns of alternating calm and active periods

    Usage:
      hawkes = HawkesProcess(mu=1.0, alpha=0.6, beta=1.5)
      for step in range(N):
          n_events = hawkes.step(dt=0.01)
          # n_events = number of order arrivals this step
    """

    def __init__(self, mu: float = 1.0, alpha: float = 0.6, beta: float = 1.5, seed: int = 42):
        assert alpha / beta < 1.0, f"α/β must be < 1 for stationarity (got {alpha / beta:.2f})"
        self.mu = mu
        self.alpha = alpha
        self.beta = beta
        self.rng = np.random.default_rng(seed)

        # Track event times for kernel computation.
        self._event_times: list[float] = []
        self._current_time: float = 0.0

    @property
    def branching_ratio(self) -> float:
        """α/β — expected number of child events per parent event."""
        return self.alpha / self.beta

    def intensity(self) -> float:
        """Compute current intensity λ(t)."""
        lam = self.mu
        for ti in self._event_times:
            lam += self.alpha * np.exp(-self.beta * (self._current_time - ti))
        return lam

    def step(self, dt: float = 0.01) -> int:
        """
        Advance time by dt and return the number of events in this interval.

        Uses Ogata's thinning algorithm for exact simulation.
        """
        self._current_time += dt
        # Prune old events (negligible kernel contribution).
        cutoff = self._current_time - 10.0 / self.beta
        self._event_times = [t for t in self._event_times if t > cutoff]

        # Upper bound on intensity.
        lam_bar = self.intensity() + self.alpha  # Conservative upper bound

        # Poisson thinning.
        n_candidates = self.rng.poisson(lam_bar * dt)
        n_events = 0
        for _ in range(n_candidates):
            if self.rng.random() < self.intensity() / max(lam_bar, 1e-10):
                n_events += 1
                self._event_times.append(self._current_time)

        return n_events

    def reset(self):
        """Reset the process state."""
        self._event_times.clear()
        self._current_time = 0.0
