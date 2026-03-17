"""
informed_trader.py — Toxic flow informed trader.

Has access to the "true price" signal (hidden from the market maker).
When the true price deviates significantly from the current mid,
sends aggressive orders to exploit the information edge.

This is the KEY adversarial agent — it creates adverse selection costs
that the market maker must manage. This dynamic is central to
Avellaneda-Stoikov's analysis of optimal market making.
"""

import numpy as np

from simulation.agents.base_agent import AgentOrder, BaseAgent
from simulation.market.tcp_client import BookUpdateMsg


class InformedTrader(BaseAgent):
    """
    Informed (toxic) trader with access to the true price.

    When true_price - mid > threshold: buy aggressively (market order)
    When mid - true_price > threshold: sell aggressively (market order)

    The `aggression` parameter controls the probability of acting.
    """

    def __init__(
        self,
        agent_id: str,
        threshold: int = 15,
        aggression: float = 0.8,
        min_qty: int = 50,
        max_qty: int = 200,
        seed: int = 42,
        **kwargs,
    ):
        super().__init__(agent_id, **kwargs)
        self.threshold = threshold
        self.aggression = aggression
        self.min_qty = min_qty
        self.max_qty = max_qty
        self.rng = np.random.default_rng(seed)
        self._true_price: int = 0

    def set_true_price(self, price: int):
        """Called by the simulator each step with the hidden true price."""
        self._true_price = price

    def on_book_update(self, update: BookUpdateMsg, step: int) -> list[AgentOrder]:
        orders = []

        if update.best_bid <= 0 or update.best_ask <= 0:
            return orders
        if self._true_price <= 0:
            return orders

        mid = (update.best_bid + update.best_ask) // 2
        edge = self._true_price - mid

        # Only trade if edge exceeds threshold AND with probability = aggression.
        if abs(edge) < self.threshold:
            return orders
        if self.rng.random() > self.aggression:
            return orders

        qty = int(self.rng.integers(self.min_qty, self.max_qty + 1))

        if edge > 0:
            # True price is higher — buy aggressively.
            order = AgentOrder(
                order_id=self.next_order_id(),
                side=0,  # BID
                order_type=1,  # MARKET
                price=0,
                quantity=qty,
            )
        else:
            # True price is lower — sell aggressively.
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
