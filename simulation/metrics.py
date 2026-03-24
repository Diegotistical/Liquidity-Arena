"""
metrics.py — Quantitative metrics engine for market making evaluation.

Computes production-grade performance metrics that a quant desk would
actually care about. These go far beyond basic PnL:

  - Sharpe Ratio: risk-adjusted returns
  - Inventory Variance: how well you control risk
  - Fill Ratio: execution quality
  - Adverse Selection: toxic flow detection (THE key metric)
  - Quote Lifetime: how quickly you cancel
  - Order-to-Trade Ratio: market making efficiency
  - PnL Decomposition: spread capture vs adverse selection vs inventory vs fees
"""

from dataclasses import dataclass

import numpy as np


@dataclass
class FillRecord:
    """Record of a single fill for metrics computation."""

    timestamp: float
    price: int
    quantity: int
    side: int  # 0=BID, 1=ASK
    is_maker: bool
    mid_at_fill: int
    mid_after_fill: int | None = None  # Set retrospectively
    maker_fee: float = 0.0
    taker_fee: float = 0.0


@dataclass
class QuoteRecord:
    """Record of a quote placement for lifetime tracking."""

    order_id: int
    timestamp: float
    cancel_timestamp: float | None = None
    was_filled: bool = False


@dataclass
class MetricsSnapshot:
    """A point-in-time metrics snapshot."""

    step: int
    pnl: float
    inventory: int
    mid_price: int
    sharpe_ratio: float = 0.0
    inventory_variance: float = 0.0
    fill_ratio: float = 0.0
    adverse_selection: float = 0.0
    quote_lifetime_avg: float = 0.0
    order_to_trade_ratio: float = 0.0
    spread_capture: float = 0.0
    inventory_pnl: float = 0.0
    fee_pnl: float = 0.0


class MetricsEngine:
    """
    Computes quantitative metrics for market making strategies.

    Usage:
        metrics = MetricsEngine()
        # During simulation:
        metrics.record_fill(fill_record)
        metrics.record_quote(quote_record)
        metrics.record_inventory(step, inventory, mid_price)
        # After simulation:
        report = metrics.compute_report()
    """

    def __init__(self, lookback_window: int = 100):
        self.lookback = lookback_window

        # State tracking.
        self._fills: list[FillRecord] = []
        self._quotes: list[QuoteRecord] = []
        self._inventory_history: list[int] = []
        self._pnl_history: list[float] = []
        self._mid_history: list[int] = []

        # Running counters.
        self._total_orders_sent: int = 0
        self._total_fills: int = 0
        self._total_cancels: int = 0

    def record_fill(self, fill: FillRecord):
        """Record a fill event."""
        self._fills.append(fill)
        self._total_fills += 1

    def record_quote(self, quote: QuoteRecord):
        """Record a quote placement."""
        self._quotes.append(quote)
        self._total_orders_sent += 1

    def record_cancel(self, order_id: int, timestamp: float):
        """Record a quote cancellation."""
        self._total_cancels += 1
        for q in reversed(self._quotes):
            if q.order_id == order_id and q.cancel_timestamp is None:
                q.cancel_timestamp = timestamp
                break

    def record_state(self, step: int, pnl: float, inventory: int, mid: int):
        """Record periodic state for time-series metrics."""
        self._pnl_history.append(pnl)
        self._inventory_history.append(inventory)
        self._mid_history.append(mid)

    # ── Core Metrics ─────────────────────────────────────────────────

    def sharpe_ratio(self) -> float:
        """
        Sharpe Ratio = mean(returns) / std(returns) * sqrt(annualization)

        Uses PnL differences as returns (step-level returns).
        Annualizes assuming 252 trading days, 6.5 hours, 1000 steps/hour.
        """
        if len(self._pnl_history) < 2:
            return 0.0
        returns = np.diff(self._pnl_history)
        if np.std(returns) < 1e-10:
            return 0.0
        # Annualize: sqrt(steps_per_year)
        steps_per_day = 6500  # 6.5 hours * 1000 steps/hour
        annualization = np.sqrt(252 * steps_per_day)
        return float(np.mean(returns) / np.std(returns) * annualization)

    def inventory_variance(self) -> float:
        """Variance of inventory path — measures how well inventory risk is controlled."""
        if len(self._inventory_history) < 2:
            return 0.0
        return float(np.var(self._inventory_history))

    def fill_ratio(self) -> float:
        """Fill Ratio = fills / orders_sent. Higher = better execution."""
        if self._total_orders_sent == 0:
            return 0.0
        return self._total_fills / self._total_orders_sent

    def adverse_selection(self) -> float:
        """
        Adverse Selection = avg(mid_after_fill - mid_at_fill)

        Positive = we're getting picked off (bad).
        Negative = we're selecting well (good).

        This is THE critical metric for market makers.
        """
        costs = []
        for fill in self._fills:
            if fill.mid_after_fill is not None and fill.is_maker:
                if fill.side == 0:  # Bought — adverse if price dropped
                    cost = fill.mid_at_fill - fill.mid_after_fill
                else:  # Sold — adverse if price rose
                    cost = fill.mid_after_fill - fill.mid_at_fill
                costs.append(cost)
        return float(np.mean(costs)) if costs else 0.0

    def quote_lifetime_avg(self) -> float:
        """Average time a quote is live before cancel/fill (in steps)."""
        lifetimes = []
        for q in self._quotes:
            if q.cancel_timestamp is not None:
                lifetimes.append(q.cancel_timestamp - q.timestamp)
            elif q.was_filled:
                lifetimes.append(
                    q.cancel_timestamp - q.timestamp if q.cancel_timestamp else 0.0
                )
        return float(np.mean(lifetimes)) if lifetimes else 0.0

    def order_to_trade_ratio(self) -> float:
        """
        Order-to-Trade Ratio = total_orders / total_fills.

        Real exchanges see 10:1 to 100:1. Lower = more efficient.
        """
        if self._total_fills == 0:
            return float("inf")
        return self._total_orders_sent / self._total_fills

    # ── PnL Decomposition ────────────────────────────────────────────

    def pnl_decomposition(self) -> dict:
        """
        Decompose PnL into components:
          PnL = spread_capture + inventory_pnl - adverse_selection - fees

        This tells you WHERE your PnL comes from — essential for
        understanding strategy performance.
        """
        spread_capture = 0.0
        adverse_sel = 0.0
        total_maker_fees = 0.0
        total_taker_fees = 0.0

        for fill in self._fills:
            if fill.is_maker:
                # Spread capture: distance from mid to fill price.
                if fill.side == 0:  # Bought below mid
                    spread_capture += fill.mid_at_fill - fill.price
                else:  # Sold above mid
                    spread_capture += fill.price - fill.mid_at_fill

                # Adverse selection cost.
                if fill.mid_after_fill is not None:
                    if fill.side == 0:
                        adverse_sel += fill.mid_at_fill - fill.mid_after_fill
                    else:
                        adverse_sel += fill.mid_after_fill - fill.mid_at_fill

                total_maker_fees += fill.maker_fee  # Negative = rebate

            total_taker_fees += fill.taker_fee if not fill.is_maker else 0.0

        # Inventory PnL from mark-to-market.
        inventory_pnl = 0.0
        if len(self._mid_history) >= 2 and len(self._inventory_history) >= 2:
            for i in range(
                1, min(len(self._mid_history), len(self._inventory_history))
            ):
                mid_change = self._mid_history[i] - self._mid_history[i - 1]
                inventory_pnl += self._inventory_history[i - 1] * mid_change

        return {
            "spread_capture": float(spread_capture),
            "inventory_pnl": float(inventory_pnl),
            "adverse_selection": float(adverse_sel),
            "maker_fees": float(total_maker_fees),
            "taker_fees": float(total_taker_fees),
            "net_fees": float(total_maker_fees + total_taker_fees),
        }

    # ── Full Report ──────────────────────────────────────────────────

    def compute_report(self) -> dict:
        """Compute all metrics and return as a dictionary."""
        decomp = self.pnl_decomposition()
        return {
            "total_fills": self._total_fills,
            "total_orders": self._total_orders_sent,
            "total_cancels": self._total_cancels,
            "sharpe_ratio": self.sharpe_ratio(),
            "inventory_variance": self.inventory_variance(),
            "fill_ratio": self.fill_ratio(),
            "adverse_selection": self.adverse_selection(),
            "quote_lifetime_avg": self.quote_lifetime_avg(),
            "order_to_trade_ratio": self.order_to_trade_ratio(),
            **decomp,
        }

    def print_report(self):
        """Print a formatted metrics report."""
        report = self.compute_report()
        print("\n" + "=" * 60)
        print("  MARKET MAKING METRICS REPORT")
        print("=" * 60)
        print(f"  Total Fills:           {report['total_fills']}")
        print(f"  Total Orders:          {report['total_orders']}")
        print(f"  Total Cancels:         {report['total_cancels']}")
        print(f"  Fill Ratio:            {report['fill_ratio']:.4f}")
        print(f"  Order-to-Trade:        {report['order_to_trade_ratio']:.1f}")
        print(f"  Sharpe Ratio:          {report['sharpe_ratio']:.2f}")
        print(f"  Inventory Variance:    {report['inventory_variance']:.2f}")
        print(f"  Adverse Selection:     {report['adverse_selection']:.2f} ticks")
        print(f"  Quote Lifetime (avg):  {report['quote_lifetime_avg']:.2f} steps")
        print("-" * 60)
        print("  PnL DECOMPOSITION:")
        print(f"    Spread Capture:      {report['spread_capture']:.2f}")
        print(f"    Inventory PnL:       {report['inventory_pnl']:.2f}")
        print(f"    Adverse Selection:   {report['adverse_selection']:.2f}")
        print(f"    Net Fees:            {report['net_fees']:.2f}")
        print("=" * 60)
