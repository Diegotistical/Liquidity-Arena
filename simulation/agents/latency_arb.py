"""
latency_arb.py — Latency arbitrage agent.

Exploits stale quotes left by slower market makers.

Strategy:
  1. Observe the "true price" signal (with low latency)
  2. Compare to current best bid/ask in the book
  3. If best_ask < true_price - threshold → buy (pick off ask)
  4. If best_bid > true_price + threshold → sell (pick off bid)

This agent is ONLY profitable when it has a latency advantage.
When latency is equalized, it can't pick off quotes before they
are updated — demonstrating why latency matters.

Key parameters:
  - threshold: minimum edge (ticks) before acting
  - max_position: inventory limit (latency arbs don't want inventory)
  - trade_prob: probability of acting when signal is present

In real markets, latency arbs are the primary source of adverse selection
for market makers. Their presence forces MMs to widen spreads and cancel
faster.
"""

import numpy as np
from typing import List
from simulation.agents.base_agent import BaseAgent, AgentOrder
from simulation.market.tcp_client import BookUpdateMsg


class LatencyArb(BaseAgent):
    """
    Latency arbitrageur that picks off stale quotes.

    This is the KEY adversarial agent that demonstrates why latency
    matters in market making. It:
    - Has faster latency than the market maker (~50µs vs ~200µs)
    - Observes the true price with minimal delay
    - Sends aggressive market orders to pick off stale quotes
    - Maintains flat inventory (not a directional trader)
    """

    def __init__(self, agent_id: str = "LAT_ARB",
                 threshold: int = 5,
                 max_position: int = 50,
                 trade_prob: float = 0.9,
                 min_qty: int = 10,
                 max_qty: int = 50,
                 seed: int = 42, **kwargs):
        super().__init__(agent_id, **kwargs)
        self.threshold = threshold
        self.max_position = max_position
        self.trade_prob = trade_prob
        self.min_qty = min_qty
        self.max_qty = max_qty
        self.rng = np.random.default_rng(seed)
        self._true_price: int = 0

    def set_true_price(self, price: int):
        """Called by the simulator each step with the true price signal."""
        self._true_price = price

    def on_book_update(self, update: BookUpdateMsg, step: int) -> List[AgentOrder]:
        orders = []

        if update.best_bid <= 0 or update.best_ask <= 0:
            return orders
        if self._true_price <= 0:
            return orders

        # Check for stale quotes to exploit.
        mid = (update.best_bid + update.best_ask) // 2

        # Ask is too low relative to true price → buy (pick off ask).
        if (self._true_price - update.best_ask) > self.threshold:
            if self.stats.inventory < self.max_position:
                if self.rng.random() < self.trade_prob:
                    qty = int(self.rng.integers(self.min_qty, self.max_qty + 1))
                    # Limit qty to not exceed position limit.
                    qty = min(qty, self.max_position - self.stats.inventory)
                    if qty > 0:
                        order = AgentOrder(
                            order_id=self.next_order_id(),
                            side=0,  # BID
                            order_type=1,  # MARKET
                            price=0,
                            quantity=qty
                        )
                        orders.append(order)
                        self.track_order(order)

        # Bid is too high relative to true price → sell (pick off bid).
        elif (update.best_bid - self._true_price) > self.threshold:
            if self.stats.inventory > -self.max_position:
                if self.rng.random() < self.trade_prob:
                    qty = int(self.rng.integers(self.min_qty, self.max_qty + 1))
                    qty = min(qty, self.max_position + self.stats.inventory)
                    if qty > 0:
                        order = AgentOrder(
                            order_id=self.next_order_id(),
                            side=1,  # ASK
                            order_type=1,  # MARKET
                            price=0,
                            quantity=qty
                        )
                        orders.append(order)
                        self.track_order(order)

        # Inventory flattening: if we have inventory, try to reduce it.
        elif abs(self.stats.inventory) > self.max_position // 2:
            if self.rng.random() < 0.3:
                if self.stats.inventory > 0:
                    qty = min(self.stats.inventory, self.max_qty)
                    order = AgentOrder(
                        order_id=self.next_order_id(),
                        side=1,  # ASK — sell to flatten
                        order_type=0,  # LIMIT — passive to reduce cost
                        price=mid + 1,
                        quantity=qty
                    )
                else:
                    qty = min(-self.stats.inventory, self.max_qty)
                    order = AgentOrder(
                        order_id=self.next_order_id(),
                        side=0,  # BID — buy to flatten
                        order_type=0,  # LIMIT
                        price=mid - 1,
                        quantity=qty
                    )
                orders.append(order)
                self.track_order(order)

        return orders
