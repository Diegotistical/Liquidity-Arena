"""
momentum_trader.py — Trend-following momentum trader.

Uses EMA crossover to detect short-term trends and sends
directional market orders. Creates autocorrelated order flow
that the market maker must handle.
"""

from typing import List

import numpy as np

from simulation.agents.base_agent import AgentOrder, BaseAgent
from simulation.market.tcp_client import BookUpdateMsg


class MomentumTrader(BaseAgent):
    """
    EMA crossover momentum trader.

    When fast_ema > slow_ema: bullish → buy
    When fast_ema < slow_ema: bearish → sell

    Creates autocorrelated (trending) order flow.
    """

    def __init__(
        self,
        agent_id: str,
        fast_period: int = 10,
        slow_period: int = 50,
        trade_prob: float = 0.3,
        min_qty: int = 20,
        max_qty: int = 80,
        seed: int = 42,
        **kwargs,
    ):
        super().__init__(agent_id, **kwargs)
        self.fast_alpha = 2.0 / (fast_period + 1)
        self.slow_alpha = 2.0 / (slow_period + 1)
        self.trade_prob = trade_prob
        self.min_qty = min_qty
        self.max_qty = max_qty
        self.rng = np.random.default_rng(seed)

        self._fast_ema: float = 0.0
        self._slow_ema: float = 0.0
        self._initialized = False

    def on_book_update(self, update: BookUpdateMsg, step: int) -> List[AgentOrder]:
        orders = []

        if update.best_bid <= 0 or update.best_ask <= 0:
            return orders

        mid = float((update.best_bid + update.best_ask) // 2)

        # Initialize EMAs.
        if not self._initialized:
            self._fast_ema = mid
            self._slow_ema = mid
            self._initialized = True
            return orders

        # Update EMAs.
        self._fast_ema = self.fast_alpha * mid + (1 - self.fast_alpha) * self._fast_ema
        self._slow_ema = self.slow_alpha * mid + (1 - self.slow_alpha) * self._slow_ema

        # Trade only probabilistically to avoid flooding.
        if self.rng.random() > self.trade_prob:
            return orders

        qty = int(self.rng.integers(self.min_qty, self.max_qty + 1))

        if self._fast_ema > self._slow_ema + 1:
            # Bullish crossover — buy.
            order = AgentOrder(
                order_id=self.next_order_id(),
                side=0,  # BID
                order_type=1,  # MARKET
                price=0,
                quantity=qty,
            )
            orders.append(order)
            self.track_order(order)

        elif self._fast_ema < self._slow_ema - 1:
            # Bearish crossover — sell.
            order = AgentOrder(
                order_id=self.next_order_id(),
                side=1,  # ASK
                order_type=1,  # MARKET
                price=0,
                quantity=qty,
            )
            orders.append(order)
            self.track_order(order)

        return orders
