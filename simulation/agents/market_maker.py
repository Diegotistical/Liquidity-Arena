"""
market_maker.py — Avellaneda-Stoikov optimal market maker.

Implements the continuous-time optimal quoting model from:
  Avellaneda, M. & Stoikov, S. (2008).
  "High-frequency trading in a limit order book."
  Quantitative Finance, 8(3), 217-224.

Core equations:
  Reservation price: r = s - q·γ·σ²·(T-t)
  Optimal spread:    δ = γ·σ²·(T-t) + (2/γ)·ln(1 + γ/κ)
  Bid: r - δ/2
  Ask: r + δ/2

Where:
  s   = current midprice
  q   = inventory (signed, positive = long)
  γ   = risk aversion parameter
  σ²  = estimated variance of midprice changes
  T-t = time remaining until end of trading horizon
  κ   = order arrival intensity
"""

import numpy as np

from simulation.agents.base_agent import AgentOrder, BaseAgent
from simulation.market.tcp_client import BookUpdateMsg


class AvellanedaStoikovMM(BaseAgent):
    """
    Avellaneda-Stoikov optimal market maker.

    This is the showcase agent. Key features:
    - Inventory-penalized reservation price (skews quotes away from risk)
    - Volatility-adaptive spread (wider when σ is high)
    - Multi-level quoting (places orders at N price levels)
    - Rolling σ² estimation from midprice changes
    - PnL decomposition: spread capture vs adverse selection
    """

    def __init__(
        self,
        agent_id: str = "MM",
        gamma: float = 0.01,
        kappa: float = 1.5,
        max_inventory: int = 100,
        num_levels: int = 3,
        level_spacing: int = 2,
        base_quantity: int = 50,
        sigma_window: int = 100,
        total_steps: int = 10000,
        seed: int = 42,
        **kwargs,
    ):
        super().__init__(agent_id, **kwargs)
        self.gamma = gamma
        self.kappa = kappa
        self.max_inventory = max_inventory
        self.num_levels = num_levels
        self.level_spacing = level_spacing
        self.base_quantity = base_quantity
        self.total_steps = total_steps
        self.rng = np.random.default_rng(seed)

        # Rolling variance estimation (Welford's algorithm).
        self._sigma_window = sigma_window
        self._mid_history: list[float] = []
        self._sigma_sq: float = 1.0  # Initial estimate

        # Track for analytics.
        self.quote_history: list[dict] = []

    def _estimate_sigma_sq(self, mid: float) -> float:
        """
        Estimate variance of midprice changes using rolling window.
        Uses simple sample variance of log-returns for robustness.
        """
        self._mid_history.append(mid)
        if len(self._mid_history) < 3:
            return self._sigma_sq  # Not enough data yet

        # Keep only the rolling window.
        if len(self._mid_history) > self._sigma_window:
            self._mid_history = self._mid_history[-self._sigma_window :]

        # Compute variance of price changes (not log returns, since ticks).
        changes = np.diff(
            self._mid_history[-min(len(self._mid_history), self._sigma_window) :]
        )
        if len(changes) > 1:
            self._sigma_sq = float(np.var(changes)) + 1e-6  # Floor to avoid zero
        return self._sigma_sq

    def on_book_update(self, update: BookUpdateMsg, step: int) -> list[AgentOrder]:
        orders_to_send = []

        if update.best_bid <= 0 or update.best_ask <= 0:
            return orders_to_send

        mid = (update.best_bid + update.best_ask) / 2.0
        self.update_unrealized_pnl(int(mid))

        # ── Step 1: Estimate σ² ──────────────────────────────────────
        sigma_sq = self._estimate_sigma_sq(mid)

        # ── Step 2: Compute time remaining (normalized to [0, 1]) ────
        t_minus_t = max(0.001, 1.0 - step / self.total_steps)

        # ── Step 3: Reservation price (A-S Eq. 10) ──────────────────
        # r = s - q·γ·σ²·(T-t)
        q = self.stats.inventory
        reservation = mid - q * self.gamma * sigma_sq * t_minus_t

        # ── Step 4: Optimal spread (A-S Eq. 12) ─────────────────────
        # δ = γ·σ²·(T-t) + (2/γ)·ln(1 + γ/κ)
        delta = self.gamma * sigma_sq * t_minus_t + (2.0 / self.gamma) * np.log(
            1 + self.gamma / self.kappa
        )

        # Minimum spread: at least 2 ticks.
        delta = max(delta, 2.0)

        # ── Step 5: Compute bid/ask prices ───────────────────────────
        bid_price = int(round(reservation - delta / 2))
        ask_price = int(round(reservation + delta / 2))

        # ── Step 6: Inventory limits ─────────────────────────────────
        can_buy = q < self.max_inventory
        can_sell = q > -self.max_inventory

        # ── Step 7: Quantity scaling (reduce near inventory limits) ──
        inv_ratio = abs(q) / self.max_inventory if self.max_inventory > 0 else 0
        qty_scale = max(0.1, 1.0 - inv_ratio * 0.8)
        qty = max(1, int(self.base_quantity * qty_scale))

        # ── Step 8: Place multi-level quotes ─────────────────────────
        for level in range(self.num_levels):
            offset = level * self.level_spacing
            level_qty = max(1, qty // (level + 1))  # Smaller at worse prices

            if can_buy:
                order = AgentOrder(
                    order_id=self.next_order_id(),
                    side=0,  # BID
                    order_type=0,  # LIMIT
                    price=bid_price - offset,
                    quantity=level_qty,
                )
                orders_to_send.append(order)
                self.track_order(order)

            if can_sell:
                order = AgentOrder(
                    order_id=self.next_order_id(),
                    side=1,  # ASK
                    order_type=0,  # LIMIT
                    price=ask_price + offset,
                    quantity=level_qty,
                )
                orders_to_send.append(order)
                self.track_order(order)

        # ── Record for analytics ─────────────────────────────────────
        self.quote_history.append(
            {
                "step": step,
                "mid": mid,
                "reservation": reservation,
                "spread": delta,
                "bid": bid_price,
                "ask": ask_price,
                "inventory": q,
                "sigma_sq": sigma_sq,
                "pnl": self.stats.total_pnl,
            }
        )

        return orders_to_send
