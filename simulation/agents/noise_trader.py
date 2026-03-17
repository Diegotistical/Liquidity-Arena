"""
noise_trader.py — Random noise trader.

Places random limit orders around the midprice and occasional market orders.
Provides liquidity and creates realistic background noise for the simulation.
"""

import numpy as np

from simulation.agents.base_agent import AgentOrder, BaseAgent
from simulation.market.tcp_client import BookUpdateMsg


class NoiseTrader(BaseAgent):
    """
    Random noise trader that provides background liquidity.

    Behavior:
    - Each step, with probability `order_prob`, places a random limit order.
    - The order is placed within `spread_range` ticks of the midprice.
    - With small probability, sends a market order instead.
    - Quantity is random within (min_qty, max_qty).
    """

    def __init__(
        self,
        agent_id: str,
        order_prob: float = 0.3,
        spread_range: int = 20,
        min_qty: int = 10,
        max_qty: int = 100,
        market_order_prob: float = 0.05,
        seed: int = 42,
        **kwargs,
    ):
        super().__init__(agent_id, **kwargs)
        self.order_prob = order_prob
        self.spread_range = spread_range
        self.min_qty = min_qty
        self.max_qty = max_qty
        self.market_order_prob = market_order_prob
        self.rng = np.random.default_rng(seed)

    def on_book_update(self, update: BookUpdateMsg, step: int) -> list[AgentOrder]:
        orders = []

        # Need a valid midprice.
        if update.best_bid <= 0 or update.best_ask <= 0:
            return orders

        mid = (update.best_bid + update.best_ask) // 2

        if self.rng.random() > self.order_prob:
            return orders

        # Random side.
        side = int(self.rng.integers(0, 2))  # 0=BID, 1=ASK
        qty = int(self.rng.integers(self.min_qty, self.max_qty + 1))

        # Market order?
        if self.rng.random() < self.market_order_prob:
            order = AgentOrder(
                order_id=self.next_order_id(),
                side=side,
                order_type=1,  # MARKET
                price=0,
                quantity=qty,
            )
            orders.append(order)
            self.track_order(order)
            return orders

        # Limit order: random offset from mid.
        offset = int(self.rng.integers(1, self.spread_range + 1))
        price = mid - offset if side == 0 else mid + offset

        order = AgentOrder(
            order_id=self.next_order_id(),
            side=side,
            order_type=0,  # LIMIT
            price=price,
            quantity=qty,
        )
        orders.append(order)
        self.track_order(order)

        return orders
