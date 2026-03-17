"""
test_price_process.py — Tests for price processes and Hawkes process.
"""

import numpy as np
import pytest

from simulation.market.price_process import (GBMProcess, HawkesProcess,
                                             OUProcess, Regime,
                                             RegimeSwitchingProcess)


class TestGBMProcess:
    def test_initial_price(self):
        gbm = GBMProcess(initial_price=10000, seed=42)
        price = gbm.step()
        assert abs(price - 10000) < 500  # Should be near initial

    def test_path_length(self):
        gbm = GBMProcess(initial_price=10000, seed=42)
        path = gbm.generate(1000)
        assert len(path) == 1000

    def test_positive_prices(self):
        """GBM should produce positive prices."""
        gbm = GBMProcess(initial_price=10000, sigma=0.001, seed=42)
        path = gbm.generate(1000)
        assert all(p > 0 for p in path)

    def test_deterministic_with_seed(self):
        path1 = GBMProcess(seed=42).generate(100)
        path2 = GBMProcess(seed=42).generate(100)
        np.testing.assert_array_equal(path1, path2)


class TestOUProcess:
    def test_mean_reversion(self):
        """OU process with high kappa should mean-revert toward theta."""
        ou = OUProcess(initial_price=12000, kappa=5.0, theta=10000, sigma=10.0, seed=42)
        path = ou.generate(1000)
        # After many steps, should be closer to theta.
        assert abs(path[-1] - 10000) < abs(12000 - 10000)

    def test_path_length(self):
        ou = OUProcess(seed=42)
        path = ou.generate(500)
        assert len(path) == 500


class TestRegimeSwitchingProcess:
    def test_starts_in_calm(self):
        rs = RegimeSwitchingProcess(seed=42)
        assert rs.current_regime == Regime.CALM

    def test_generates_prices(self):
        rs = RegimeSwitchingProcess(initial_price=10000, seed=42)
        path = rs.generate(1000)
        assert len(path) == 1000
        assert all(p > 0 for p in path)

    def test_regime_changes_eventually(self):
        """Over many steps, regime should change at least once."""
        rs = RegimeSwitchingProcess(seed=42)
        regimes_seen = set()
        for _ in range(5000):
            rs.step()
            regimes_seen.add(rs.current_regime)
        assert len(regimes_seen) > 1, "Should see multiple regimes over 5000 steps"


class TestHawkesProcess:
    def test_stationarity_check(self):
        """α/β ≥ 1 should raise assertion error."""
        with pytest.raises(AssertionError):
            HawkesProcess(mu=1.0, alpha=1.5, beta=1.0)

    def test_branching_ratio(self):
        hp = HawkesProcess(mu=1.0, alpha=0.6, beta=1.5)
        assert abs(hp.branching_ratio - 0.4) < 0.01

    def test_generates_events(self):
        """Hawkes process should generate some events over many steps."""
        hp = HawkesProcess(mu=1.0, alpha=0.6, beta=1.5, seed=42)
        total_events = sum(hp.step(dt=0.01) for _ in range(1000))
        assert total_events > 0

    def test_self_exciting(self):
        """Intensity should increase after events."""
        hp = HawkesProcess(mu=1.0, alpha=0.6, beta=1.5, seed=42)
        base_intensity = hp.intensity()
        # Force some events.
        hp._event_times.append(hp._current_time)
        hp._event_times.append(hp._current_time)
        assert hp.intensity() > base_intensity

    def test_reset(self):
        hp = HawkesProcess(seed=42)
        for _ in range(100):
            hp.step()
        hp.reset()
        assert hp.intensity() == hp.mu
        assert len(hp._event_times) == 0
