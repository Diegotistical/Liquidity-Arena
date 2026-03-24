"""
test_integration.py — End-to-end integration tests.

Tests the full pipeline: agents → TCP client → C++ engine → fills → metrics.
Runs without the C++ engine by testing the Python-side logic end-to-end.
"""

from simulation.agents.base_agent import AgentOrder
from simulation.agents.informed_trader import InformedTrader
from simulation.agents.latency_arb import LatencyArb
from simulation.agents.market_maker import AvellanedaStoikovMM
from simulation.agents.momentum_trader import MomentumTrader
from simulation.agents.noise_trader import NoiseTrader
from simulation.market.tcp_client import BookUpdateMsg, FillMsg, NewOrderMsg
from simulation.metrics import FillRecord, MetricsEngine, QuoteRecord


class TestOrderIdUniqueness:
    """Verify that order IDs are globally unique across agents."""

    def test_no_id_collisions_across_agents(self):
        """All agents should produce unique order IDs."""
        mm = AvellanedaStoikovMM(agent_id="MM", seed=42)
        noise = NoiseTrader(agent_id="NOISE_0", seed=43)
        informed = InformedTrader(agent_id="INFORMED", seed=44)
        momentum = MomentumTrader(agent_id="MOMENTUM", seed=45)
        lat_arb = LatencyArb(agent_id="LAT_ARB", seed=46)

        all_ids = set()
        for agent in [mm, noise, informed, momentum, lat_arb]:
            for _ in range(100):
                oid = agent.next_order_id()
                assert (
                    oid not in all_ids
                ), f"Duplicate ID {oid} from agent {agent.agent_id}"
                all_ids.add(oid)

        assert len(all_ids) == 500  # 5 agents × 100 IDs each

    def test_id_offset_spacing(self):
        """Order IDs should be spaced by at least 1M per agent."""
        agents = [NoiseTrader(agent_id=f"A{i}", seed=i) for i in range(5)]
        first_ids = [a.next_order_id() for a in agents]
        for i in range(1, len(first_ids)):
            assert first_ids[i] - first_ids[i - 1] >= 1_000_000


class TestFillMatching:
    """Verify that on_fill correctly matches fills to active orders."""

    def test_fill_matches_maker_id(self):
        """Agent should match a fill where its order is the maker."""
        mm = AvellanedaStoikovMM(agent_id="MM", seed=42)
        order = AgentOrder(
            order_id=mm.next_order_id(),
            side=0,  # BID
            order_type=0,
            price=10000,
            quantity=100,
        )
        mm.track_order(order)

        # Simulate a fill where our order is the maker.
        fill = FillMsg(
            maker_id=order.order_id,
            taker_id=999999,
            price=10000,
            quantity=50,
        )
        mm.on_fill(fill)

        assert mm.stats.total_fills == 1
        assert mm.stats.inventory == 50  # Bought 50
        assert mm.stats.total_volume == 50

    def test_fill_matches_taker_id(self):
        """Agent should match a fill where its order is the taker."""
        noise = NoiseTrader(agent_id="NOISE", seed=42)
        order = AgentOrder(
            order_id=noise.next_order_id(),
            side=1,  # ASK
            order_type=1,
            price=0,
            quantity=30,
        )
        noise.track_order(order)

        fill = FillMsg(
            maker_id=888888,
            taker_id=order.order_id,
            price=10050,
            quantity=30,
        )
        noise.on_fill(fill)

        assert noise.stats.total_fills == 1
        assert noise.stats.inventory == -30  # Sold 30

    def test_fill_ignored_if_not_our_order(self):
        """Agent should silently ignore fills for other agents' orders."""
        mm = AvellanedaStoikovMM(agent_id="MM", seed=42)

        fill = FillMsg(
            maker_id=999999,
            taker_id=888888,
            price=10000,
            quantity=100,
        )
        mm.on_fill(fill)

        assert mm.stats.total_fills == 0
        assert mm.stats.inventory == 0


class TestCancelAndReplace:
    """Verify the market maker's cancel-and-replace logic."""

    def test_mm_generates_cancels(self):
        """Market maker should cancel old quotes before placing new ones."""
        mm = AvellanedaStoikovMM(
            agent_id="MM",
            gamma=0.1,
            kappa=1.5,
            num_levels=2,
            base_quantity=100,
            seed=42,
        )

        book = BookUpdateMsg(
            best_bid=9990,
            best_ask=10010,
            num_bid_levels=1,
            num_ask_levels=1,
            bid_prices=[9990],
            bid_quantities=[100],
            ask_prices=[10010],
            ask_quantities=[100],
        )

        # First call — no cancels (nothing to cancel).
        orders1 = mm.on_book_update(book, step=100)
        assert len(orders1) > 0
        assert len(mm._pending_cancels) == 0  # Nothing was active before

        # Second call — should cancel the orders from step 1.
        orders2 = mm.on_book_update(book, step=200)
        assert len(orders2) > 0
        assert len(mm._pending_cancels) > 0  # Should have pending cancels


class TestTcpProtocol:
    """Verify TCP serialization matches C++ struct layout."""

    def test_new_order_msg_includes_display_qty(self):
        """NewOrderMsg should serialize display_qty for iceberg support."""
        msg = NewOrderMsg(
            id=12345,
            side=0,
            type=2,  # ICEBERG
            price=10000,
            quantity=1000,
            display_qty=100,
        )
        packed = msg.pack()
        assert len(packed) == NewOrderMsg.SIZE

        # Verify the display_qty is in the packed bytes.
        import struct

        unpacked = struct.unpack(NewOrderMsg.FORMAT, packed)
        assert unpacked[0] == 12345  # id
        assert unpacked[1] == 0  # side
        assert unpacked[2] == 2  # type (ICEBERG)
        assert unpacked[3] == 10000  # price
        assert unpacked[4] == 1000  # quantity
        assert unpacked[5] == 100  # display_qty

    def test_new_order_default_display_qty(self):
        """Regular limit orders should have display_qty=0 by default."""
        msg = NewOrderMsg(id=1, side=0, type=0, price=10000, quantity=100)
        assert msg.display_qty == 0

        packed = msg.pack()
        import struct

        unpacked = struct.unpack(NewOrderMsg.FORMAT, packed)
        assert unpacked[5] == 0  # display_qty defaults to 0


class TestMetricsIntegration:
    """Verify metrics engine works with the full agent pipeline."""

    def test_full_metrics_pipeline(self):
        """Record fills, quotes, cancels and verify report generation."""
        metrics = MetricsEngine()

        # Record some quotes.
        for i in range(10):
            metrics.record_quote(QuoteRecord(order_id=i, timestamp=float(i)))

        # Record some fills.
        for i in range(5):
            metrics.record_fill(
                FillRecord(
                    timestamp=float(i),
                    price=10000 + i,
                    quantity=100,
                    side=0,
                    is_maker=True,
                    mid_at_fill=10000,
                    mid_after_fill=10000 + i,
                )
            )

        # Record some cancels.
        for i in range(5, 10):
            metrics.record_cancel(i, float(i + 1))

        # Record state snapshots.
        for i in range(20):
            metrics.record_state(
                step=i, pnl=float(i * 10), inventory=i % 5, mid=10000 + i
            )

        report = metrics.compute_report()
        assert report["total_fills"] == 5
        assert report["total_orders"] == 10
        assert report["total_cancels"] == 5
        assert report["fill_ratio"] == 0.5
        assert report["order_to_trade_ratio"] == 2.0
        assert "spread_capture" in report
        assert "inventory_pnl" in report
        assert "adverse_selection" in report
