"""
human_agent.py — Player-controlled agent that translates UI clicks into orders.
"""
from simulation.agents.base_agent import AgentOrder, BaseAgent
from simulation.market.tcp_client import BookUpdateMsg


class HumanAgent(BaseAgent):
    """An agent fully controlled by the frontend WebSocket."""

    def __init__(self, agent_id: str = "HUMAN"):
        super().__init__(agent_id)
        self._pending_orders: list[AgentOrder] = []
        self._pending_cancels: list[int] = []
        self.last_book: BookUpdateMsg | None = None

    def on_book_update(self, update: BookUpdateMsg, step: int) -> list[AgentOrder]:
        """Submit whatever orders were queued by the UI and track the latest book."""
        self.last_book = update
        
        # Return pending orders and flush queue.
        orders = self._pending_orders.copy()
        self._pending_orders.clear()
        
        # Track active orders for fill matching
        for order in orders:
            self.track_order(order)
            
        return orders

    def place_market_order(self, side: int, qty: int):
        """0=BID, 1=ASK"""
        self._pending_orders.append(AgentOrder(
            order_id=self.next_order_id(),
            side=side,
            order_type=1,  # 1=MARKET
            price=0,
            quantity=qty
        ))

    def place_limit_order(self, side: int, offset: int, qty: int):
        """Place limit order at best price + offset. 0=BID, 1=ASK"""
        if not self.last_book:
            return
            
        if side == 0:  # BID
            price = self.last_book.best_bid - offset
        else:          # ASK
            price = getattr(self.last_book, 'best_ask') + offset
            
        if price <= 0:
            return
            
        self._pending_orders.append(AgentOrder(
            order_id=self.next_order_id(),
            side=side,
            order_type=0,  # 0=LIMIT
            price=price,
            quantity=qty
        ))
        
    def cancel_all_quotes(self):
        """Cancel all active limit orders currently on the book."""
        # BaseAgent's cancel_all returns IDs of active orders and clears the dict
        self._pending_cancels.extend(self.cancel_all())
