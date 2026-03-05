"""
test_metrics.py — Tests for the quantitative metrics engine.
"""

import pytest
import numpy as np
from simulation.metrics import MetricsEngine, FillRecord, QuoteRecord


class TestMetricsEngine:
    def setup_method(self):
        self.metrics = MetricsEngine()

    def test_sharpe_ratio_flat(self):
        """Zero returns → zero Sharpe."""
        for i in range(100):
            self.metrics.record_state(i, pnl=100.0, inventory=0, mid=10000)
        assert self.metrics.sharpe_ratio() == 0.0

    def test_sharpe_ratio_positive(self):
        """Steadily increasing PnL → positive Sharpe."""
        for i in range(100):
            self.metrics.record_state(i, pnl=float(i * 10), inventory=0, mid=10000)
        assert self.metrics.sharpe_ratio() > 0

    def test_inventory_variance_flat(self):
        """Constant inventory → zero variance."""
        for i in range(100):
            self.metrics.record_state(i, pnl=0, inventory=50, mid=10000)
        assert self.metrics.inventory_variance() == 0.0

    def test_inventory_variance_volatile(self):
        """Oscillating inventory → high variance."""
        for i in range(100):
            inv = 100 if i % 2 == 0 else -100
            self.metrics.record_state(i, pnl=0, inventory=inv, mid=10000)
        assert self.metrics.inventory_variance() > 0

    def test_fill_ratio(self):
        """Fill ratio = fills / orders."""
        for i in range(10):
            self.metrics.record_quote(QuoteRecord(order_id=i, timestamp=float(i)))
        for i in range(3):
            self.metrics.record_fill(FillRecord(
                timestamp=float(i), price=10000, quantity=100,
                side=0, is_maker=True, mid_at_fill=10000
            ))
        assert abs(self.metrics.fill_ratio() - 0.3) < 0.01

    def test_order_to_trade_ratio(self):
        """OTR = orders / trades."""
        for i in range(20):
            self.metrics.record_quote(QuoteRecord(order_id=i, timestamp=float(i)))
        for i in range(5):
            self.metrics.record_fill(FillRecord(
                timestamp=float(i), price=10000, quantity=100,
                side=0, is_maker=True, mid_at_fill=10000
            ))
        assert abs(self.metrics.order_to_trade_ratio() - 4.0) < 0.01

    def test_adverse_selection_neutral(self):
        """No post-fill data → zero adverse selection."""
        self.metrics.record_fill(FillRecord(
            timestamp=0, price=10000, quantity=100,
            side=0, is_maker=True, mid_at_fill=10000
        ))
        assert self.metrics.adverse_selection() == 0.0

    def test_adverse_selection_negative(self):
        """Price moved in our favor after fill."""
        self.metrics.record_fill(FillRecord(
            timestamp=0, price=9990, quantity=100,
            side=0, is_maker=True, mid_at_fill=10000,
            mid_after_fill=10010  # Price went up after we bought
        ))
        # Bought, price went up → favorable → positive adverse_sel in our formula
        result = self.metrics.adverse_selection()
        assert result != 0.0, "Should have non-zero adverse selection"

    def test_pnl_decomposition_keys(self):
        """PnL decomposition should have all expected keys."""
        decomp = self.metrics.pnl_decomposition()
        expected_keys = {"spread_capture", "inventory_pnl", "adverse_selection",
                         "maker_fees", "taker_fees", "net_fees"}
        assert expected_keys == set(decomp.keys())

    def test_compute_report(self):
        """Full report should contain all metrics."""
        for i in range(100):
            self.metrics.record_state(i, pnl=float(i), inventory=i % 10, mid=10000)
        report = self.metrics.compute_report()
        assert "sharpe_ratio" in report
        assert "fill_ratio" in report
        assert "adverse_selection" in report
        assert "spread_capture" in report
