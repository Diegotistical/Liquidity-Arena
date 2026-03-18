"""
test_agents.py — Unit tests for all simulation agents.

Tests verify:
  - Agents produce valid orders given book updates
  - Market orders have correct type/side
  - Informed trader respects edge thresholds
  - Latency arb respects position limits
  - Noise trader random behavior is seeded/reproducible
"""

from dataclasses import dataclass

# ── Mock BookUpdateMsg ────────────────────────────────────────────────
# We mock the TCP client's message type so tests don't need a real engine.


@dataclass
class MockBookUpdate:
    """Mock BookUpdateMsg for testing agents without TCP connection."""

    best_bid: int = 9995
    best_ask: int = 10005
    num_bid_levels: int = 5
    num_ask_levels: int = 5
    bid_prices: list = None
    bid_quantities: list = None
    ask_prices: list = None
    ask_quantities: list = None

    def __post_init__(self):
        if self.bid_prices is None:
            self.bid_prices = [9995, 9990, 9985, 9980, 9975]
        if self.bid_quantities is None:
            self.bid_quantities = [100, 200, 150, 300, 100]
        if self.ask_prices is None:
            self.ask_prices = [10005, 10010, 10015, 10020, 10025]
        if self.ask_quantities is None:
            self.ask_quantities = [100, 200, 150, 300, 100]


# Monkey-patch the import so agents use our mock
import sys  # noqa: E402
from unittest.mock import MagicMock  # noqa: E402

mock_tcp = MagicMock()
mock_tcp.BookUpdateMsg = MockBookUpdate
sys.modules["simulation.market.tcp_client"] = mock_tcp

from simulation.agents.informed_trader import InformedTrader  # noqa: E402
from simulation.agents.latency_arb import LatencyArb  # noqa: E402
from simulation.agents.momentum_trader import MomentumTrader  # noqa: E402
from simulation.agents.noise_trader import NoiseTrader  # noqa: E402

# ═══════════════════════════════════════════════════════════════════════
# Noise Trader Tests
# ═══════════════════════════════════════════════════════════════════════


class TestNoiseTrader:
    def setup_method(self):
        self.trader = NoiseTrader(agent_id="NOISE_0", seed=42)
        self.update = MockBookUpdate()

    def test_produces_orders(self):
        """Noise trader should produce orders with sufficient calls."""
        all_orders = []
        for step in range(100):
            orders = self.trader.on_book_update(self.update, step)
            all_orders.extend(orders)
        assert len(all_orders) > 0, "Noise trader should produce some orders over 100 steps"

    def test_order_valid_sides(self):
        """All orders should have valid side (0=BID or 1=ASK)."""
        for step in range(100):
            for order in self.trader.on_book_update(self.update, step):
                assert order.side in (0, 1)

    def test_order_valid_quantities(self):
        """Quantities should be within configured range."""
        for step in range(100):
            for order in self.trader.on_book_update(self.update, step):
                assert 10 <= order.quantity <= 100

    def test_handles_invalid_book(self):
        """Should return empty list when book has invalid prices."""
        bad_update = MockBookUpdate(best_bid=-1, best_ask=-1)
        orders = self.trader.on_book_update(bad_update, 0)
        assert len(orders) == 0

    def test_deterministic_with_seed(self):
        """Same seed should produce same order sequence."""
        trader1 = NoiseTrader(agent_id="A", seed=42)
        trader2 = NoiseTrader(agent_id="B", seed=42)
        for step in range(50):
            o1 = trader1.on_book_update(self.update, step)
            o2 = trader2.on_book_update(self.update, step)
            assert len(o1) == len(o2)


# ═══════════════════════════════════════════════════════════════════════
# Informed Trader Tests
# ═══════════════════════════════════════════════════════════════════════


class TestInformedTrader:
    def setup_method(self):
        self.trader = InformedTrader(agent_id="INFORMED", threshold=15, seed=42)
        self.update = MockBookUpdate()

    def test_no_trade_without_edge(self):
        """Should not trade when true price is near mid."""
        mid = (self.update.best_bid + self.update.best_ask) // 2
        self.trader.set_true_price(mid)
        orders = self.trader.on_book_update(self.update, 0)
        assert len(orders) == 0

    def test_buys_when_true_price_higher(self):
        """Should buy when true price is significantly above mid."""
        self.trader.set_true_price(10100)  # Well above mid of ~10000
        all_orders = []
        for step in range(20):
            orders = self.trader.on_book_update(self.update, step)
            all_orders.extend(orders)
        buy_orders = [o for o in all_orders if o.side == 0]
        assert len(buy_orders) > 0, "Should buy when true price is high"

    def test_sells_when_true_price_lower(self):
        """Should sell when true price is significantly below mid."""
        self.trader.set_true_price(9900)  # Well below mid
        all_orders = []
        for step in range(20):
            orders = self.trader.on_book_update(self.update, step)
            all_orders.extend(orders)
        sell_orders = [o for o in all_orders if o.side == 1]
        assert len(sell_orders) > 0, "Should sell when true price is low"

    def test_uses_market_orders(self):
        """Informed trader should use market orders (aggressive)."""
        self.trader.set_true_price(10100)
        for step in range(20):
            for order in self.trader.on_book_update(self.update, step):
                assert order.order_type == 1  # MARKET


# ═══════════════════════════════════════════════════════════════════════
# Momentum Trader Tests
# ═══════════════════════════════════════════════════════════════════════


class TestMomentumTrader:
    def setup_method(self):
        self.trader = MomentumTrader(agent_id="MOMENTUM", seed=42)

    def test_no_trade_on_first_step(self):
        """Should not trade on first step (needs to initialize EMAs)."""
        update = MockBookUpdate()
        orders = self.trader.on_book_update(update, 0)
        assert len(orders) == 0

    def test_responds_to_trend(self):
        """After a sustained uptrend, should eventually buy."""
        all_orders = []
        for step in range(200):
            # Simulate uptrend: prices moving up.
            price = 10000 + step * 2
            update = MockBookUpdate(best_bid=price - 5, best_ask=price + 5)
            orders = self.trader.on_book_update(update, step)
            all_orders.extend(orders)
        # After a strong uptrend, should have some buy orders.
        buy_orders = [o for o in all_orders if o.side == 0]
        assert len(buy_orders) > 0, "Should buy during uptrend"


# ═══════════════════════════════════════════════════════════════════════
# Latency Arb Tests
# ═══════════════════════════════════════════════════════════════════════


class TestLatencyArb:
    def setup_method(self):
        self.trader = LatencyArb(agent_id="LAT_ARB", threshold=5, seed=42)
        self.update = MockBookUpdate()

    def test_no_trade_without_edge(self):
        """Should not trade when book prices match true price."""
        mid = (self.update.best_bid + self.update.best_ask) // 2
        self.trader.set_true_price(mid)
        orders = self.trader.on_book_update(self.update, 0)
        assert len(orders) == 0

    def test_buys_stale_ask(self):
        """Should buy when ask is below true price (pick off stale ask)."""
        # True price is much higher than best ask.
        self.trader.set_true_price(10050)
        all_orders = []
        for step in range(10):
            orders = self.trader.on_book_update(self.update, step)
            all_orders.extend(orders)
        buy_orders = [o for o in all_orders if o.side == 0]
        assert len(buy_orders) > 0, "Should buy when ask is stale"

    def test_respects_position_limit(self):
        """Should not exceed max_position."""
        self.trader.set_true_price(10050)
        self.trader.stats.inventory = 50  # Already at max
        orders = self.trader.on_book_update(self.update, 0)
        buy_orders = [o for o in orders if o.side == 0 and o.order_type == 1]
        assert len(buy_orders) == 0, "Should not buy when at position limit"
