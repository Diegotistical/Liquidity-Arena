"""
price_process.py — Synthetic true price generators.

Provides the "fundamental value" that informed traders observe.
The market maker does NOT see this — the information asymmetry is
the key microstructure dynamic driving adverse selection.

Supported processes:
  - GBM (Geometric Brownian Motion): dS = μ·S·dt + σ·S·dW
  - OU (Ornstein-Uhlenbeck): dS = κ(θ − S)·dt + σ·dW
"""

import numpy as np
from abc import ABC, abstractmethod


class PriceProcess(ABC):
    """Abstract base class for price processes."""

    @abstractmethod
    def generate(self, num_steps: int) -> np.ndarray:
        """Generate a price path (in integer ticks)."""
        pass

    @abstractmethod
    def step(self) -> int:
        """Generate the next price tick (online mode)."""
        pass


class GBMProcess(PriceProcess):
    """
    Geometric Brownian Motion.
    dS = μ·S·dt + σ·S·dW (Euler-Maruyama discretization)
    """

    def __init__(self, initial_price: int = 10000, mu: float = 0.0,
                 sigma: float = 0.005, dt: float = 0.001, seed: int = 42):
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
            dW = self.rng.normal(0, np.sqrt(self.dt))
            s *= (1 + self.mu * self.dt + self.sigma * dW)
            prices[i] = int(round(s))
        return prices

    def step(self) -> int:
        dW = self.rng.normal(0, np.sqrt(self.dt))
        self.current_price *= (1 + self.mu * self.dt + self.sigma * dW)
        return int(round(self.current_price))


class OUProcess(PriceProcess):
    """
    Ornstein-Uhlenbeck (mean-reverting) process.
    dS = κ(θ − S)·dt + σ·dW

    More realistic for tick-level simulation: prices mean-revert
    over short horizons, creating a stationary spread.
    """

    def __init__(self, initial_price: int = 10000, kappa: float = 0.5,
                 theta: int = 10000, sigma: float = 50.0,
                 dt: float = 0.001, seed: int = 42):
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
            dW = self.rng.normal(0, np.sqrt(self.dt))
            s += self.kappa * (self.theta - s) * self.dt + self.sigma * dW
            prices[i] = int(round(s))
        return prices

    def step(self) -> int:
        dW = self.rng.normal(0, np.sqrt(self.dt))
        self.current_price += (self.kappa * (self.theta - self.current_price) * self.dt
                                + self.sigma * dW)
        return int(round(self.current_price))
