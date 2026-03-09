"""
base_agent.py — Abstract base class for all trading agents.

Each agent:
  1. Receives BookUpdate messages from the engine
  2. Decides what orders to place/cancel
  3. Tracks its own inventory and PnL
"""

from abc import ABC, abstractmethod
from dataclasses import dataclass

from simulation.market.tcp_client import BookUpdateMsg, FillMsg


@dataclass
class AgentOrder:
    """Order that an agent wants to send."""

    order_id: int
    side: int  # 0=BID, 1=ASK
    order_type: int  # 0=LIMIT, 1=MARKET
    price: int  # Integer ticks
    quantity: int


@dataclass
class AgentStats:
    """Running statistics for an agent."""

    total_pnl: float = 0.0
    realized_pnl: float = 0.0
    unrealized_pnl: float = 0.0
    inventory: int = 0
    total_fills: int = 0
    total_orders_sent: int = 0
    total_volume: int = 0
    avg_fill_price: float = 0.0
    # For PnL decomposition
    spread_pnl: float = 0.0
    inventory_pnl: float = 0.0


class BaseAgent(ABC):
    """Abstract trading agent."""

    def __init__(self, agent_id: str, tick_size: float = 0.01):
        self.agent_id = agent_id
        self.tick_size = tick_size
        self.stats = AgentStats()
        self._next_order_id = 1
        self._active_orders: dict[int, AgentOrder] = {}
        self._fill_prices: list[tuple[int, int]] = []  # (price, signed_qty)
        self._mid_price: int = 0

    def next_order_id(self) -> int:
        """Generate a unique order ID for this agent."""
        oid = self._next_order_id
        self._next_order_id += 1
        self.stats.total_orders_sent += 1
        return oid

    @abstractmethod
    def on_book_update(self, update: BookUpdateMsg, step: int) -> list[AgentOrder]:
        """
        React to a book update. Returns a list of orders to send.
        This is the main decision function — called every simulation step.
        """

    def on_fill(self, fill: FillMsg, my_order_id: int):
        """Handle a fill notification for one of our orders."""
        order = self._active_orders.get(my_order_id)
        if order is None:
            return

        qty = fill.quantity
        if order.side == 1:  # ASK — we sold
            qty = -qty

        self.stats.inventory += qty
        self.stats.total_fills += 1
        self.stats.total_volume += abs(fill.quantity)

        # Track realized PnL (FIFO-style cash flow)
        # Selling: +price*qty, Buying: -price*qty.
        # Negative qty for buys, positive for sells.
        cash_flow = -fill.price * qty
        self.stats.realized_pnl += cash_flow * self.tick_size

        self._fill_prices.append((fill.price, qty))

        # Remove fully filled orders.
        if order.quantity <= fill.quantity:
            self._active_orders.pop(my_order_id, None)

    def update_unrealized_pnl(self, mid_price: int):
        """Update unrealized PnL based on current mid price."""
        self._mid_price = mid_price
        self.stats.unrealized_pnl = self.stats.inventory * mid_price * self.tick_size
        self.stats.total_pnl = self.stats.realized_pnl + self.stats.unrealized_pnl

    def track_order(self, order: AgentOrder):
        """Register an active order."""
        self._active_orders[order.order_id] = order

    def cancel_all(self) -> list[int]:
        """Return IDs of all active orders to cancel."""
        ids = list(self._active_orders.keys())
        self._active_orders.clear()
        return ids
