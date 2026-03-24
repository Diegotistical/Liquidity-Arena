"""
simulator.py — Event-driven microstructure simulator.

Core architecture:
  while events:
      event = pop()
      process(event)

This replaces the naive time-step loop with a proper event queue.
Events include:
  - MIDPRICE_MOVE:  True price updates (from price process)
  - AGENT_ACTION:   Agent generates quotes/orders (with latency delay)
  - ORDER_CANCEL:   Stale quote cancellation
  - BOOK_UPDATE:    LOB state broadcast to agents
  - METRICS_SAMPLE: Periodic metrics recording

Each agent schedules its own events with latency offsets.
This means faster agents genuinely see stale quotes first.
"""

import heapq
import json
import logging
import signal
import time
from dataclasses import dataclass, field
from enum import IntEnum
from typing import Any

import yaml

from simulation.agents.base_agent import AgentOrder, BaseAgent
from simulation.agents.informed_trader import InformedTrader
from simulation.agents.latency_arb import LatencyArb
from simulation.agents.market_maker import AvellanedaStoikovMM
from simulation.agents.momentum_trader import MomentumTrader
from simulation.agents.noise_trader import NoiseTrader
from simulation.market.latency_model import (LatencyConfig, LatencyModel,
                                             latency_arb_latency,
                                             market_maker_latency,
                                             retail_latency)
from simulation.market.price_process import (GBMProcess, HawkesProcess,
                                             OUProcess, RegimeSwitchingProcess)
from simulation.market.tcp_client import BookUpdateMsg, TcpClient
from simulation.metrics import FillRecord, MetricsEngine, QuoteRecord

logging.basicConfig(
    level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s"
)
log = logging.getLogger("simulator")


# ═══════════════════════════════════════════════════════════════════════
# Event System
# ═══════════════════════════════════════════════════════════════════════


class EventType(IntEnum):
    MIDPRICE_MOVE = 0
    AGENT_ACTION = 1
    ORDER_CANCEL = 2
    BOOK_UPDATE = 3
    METRICS_SAMPLE = 4


@dataclass(order=True)
class Event:
    """A timestamped simulation event."""

    timestamp: float
    event_type: EventType = field(compare=False)
    agent_id: str | None = field(default=None, compare=False)
    data: Any = field(default=None, compare=False)


class EventQueue:
    """Min-heap priority queue for events."""

    def __init__(self):
        self._heap: list[Event] = []

    def push(self, event: Event):
        heapq.heappush(self._heap, event)

    def pop(self) -> Event:
        return heapq.heappop(self._heap)

    def __len__(self) -> int:
        return len(self._heap)

    def __bool__(self) -> bool:
        return len(self._heap) > 0


# ═══════════════════════════════════════════════════════════════════════
# Simulator
# ═══════════════════════════════════════════════════════════════════════


class Simulator:
    """
    Event-driven microstructure simulator.

    Orchestrates:
      - C++ matching engine (via TCP)
      - Price process (fundamental value)
      - Multi-agent environment
      - Hawkes-driven order flow
      - Per-agent latency model
      - Quantitative metrics collection
    """

    def __init__(self, config_path: str = "simulation/config/default.yaml"):
        self.config = self._load_config(config_path)
        self.running = False
        self.current_time = 0.0

        # Core components.
        self.event_queue = EventQueue()
        self.tcp_client: TcpClient | None = None
        self.ws_bridge = None
        self.price_process = self._create_price_process()
        self.hawkes: HawkesProcess | None = None
        self.metrics = MetricsEngine()

        # Agents.
        self.agents: dict[str, BaseAgent] = {}
        self.agent_latencies: dict[str, LatencyModel] = {}

        # State.
        self.current_mid: int = self.config["price_process"]["initial_price"]
        self.last_book_update: BookUpdateMsg | None = None
        self.step_count: int = 0
        self.total_steps: int = self.config["simulation"]["steps"]

        # Cancel tracking for metrics.
        self._pending_cancels: dict[int, float] = {}  # order_id → scheduled cancel time

    def _load_config(self, path: str) -> dict:
        with open(path, encoding="utf-8") as f:
            return yaml.safe_load(f)

    def _create_price_process(self):
        pp_config = self.config["price_process"]
        pp_type = pp_config.get("type", "ou")
        initial = pp_config["initial_price"]

        if pp_type == "gbm":
            return GBMProcess(
                initial_price=initial,
                mu=pp_config.get("mu", 0.0),
                sigma=pp_config.get("sigma", 0.005),
                seed=pp_config.get("seed", 42),
            )
        elif pp_type == "regime_switching":
            return RegimeSwitchingProcess(
                initial_price=initial,
                kappa=pp_config.get("kappa", 0.3),
                theta=pp_config.get("theta", initial),
                seed=pp_config.get("seed", 42),
            )
        else:  # Default: OU
            return OUProcess(
                initial_price=initial,
                kappa=pp_config.get("kappa", 0.5),
                theta=pp_config.get("theta", initial),
                sigma=pp_config.get("sigma", 50.0),
                seed=pp_config.get("seed", 42),
            )

    def _create_agents(self):
        """Create agents from config."""
        ac = self.config.get("agents", {})

        # Market Maker (Avellaneda-Stoikov).
        mm_cfg = ac.get("market_maker", {})
        mm = AvellanedaStoikovMM(
            agent_id="MM",
            gamma=mm_cfg.get("gamma", 0.1),
            kappa=mm_cfg.get("kappa", 1.5),
            num_levels=mm_cfg.get("levels", 3),
            level_spacing=mm_cfg.get("level_spacing", 5),
            base_quantity=mm_cfg.get("base_qty", 100),
            max_inventory=mm_cfg.get("inventory_limit", 500),
        )
        self.agents["MM"] = mm
        self.agent_latencies["MM"] = market_maker_latency(seed=42)

        # Noise traders.
        for i in range(ac.get("num_noise", 3)):
            agent_id = f"NOISE_{i}"
            nt_cfg = ac.get("noise_trader", {})
            nt = NoiseTrader(
                agent_id=agent_id,
                order_prob=nt_cfg.get("order_prob", 0.3),
                spread_range=nt_cfg.get("spread_range", 20),
                min_qty=nt_cfg.get("min_qty", 10),
                max_qty=nt_cfg.get("max_qty", 100),
                market_order_prob=nt_cfg.get("market_order_prob", 0.05),
                seed=42 + i,
            )
            self.agents[agent_id] = nt
            self.agent_latencies[agent_id] = retail_latency(seed=42 + i)

        # Informed trader.
        it_cfg = ac.get("informed_trader", {})
        it = InformedTrader(
            agent_id="INFORMED",
            threshold=it_cfg.get("threshold", 15),
            aggression=it_cfg.get("aggression", 0.8),
            min_qty=it_cfg.get("min_qty", 50),
            max_qty=it_cfg.get("max_qty", 200),
            seed=100,
        )
        self.agents["INFORMED"] = it
        self.agent_latencies["INFORMED"] = LatencyModel(
            LatencyConfig(base_us=500.0, jitter_us=100.0), seed=100
        )

        # Momentum trader.
        mt_cfg = ac.get("momentum_trader", {})
        mt = MomentumTrader(
            agent_id="MOMENTUM",
            fast_period=mt_cfg.get("fast_period", 10),
            slow_period=mt_cfg.get("slow_period", 50),
            trade_prob=mt_cfg.get("trade_prob", 0.3),
            min_qty=mt_cfg.get("min_qty", 20),
            max_qty=mt_cfg.get("max_qty", 80),
            seed=200,
        )
        self.agents["MOMENTUM"] = mt
        self.agent_latencies["MOMENTUM"] = retail_latency(seed=200)

        # Latency arbitrageur.
        la_cfg = ac.get("latency_arb", {})
        la = LatencyArb(
            agent_id="LAT_ARB",
            threshold=la_cfg.get("threshold", 5),
            max_position=la_cfg.get("max_position", 50),
            trade_prob=la_cfg.get("trade_prob", 0.9),
            seed=300,
        )
        self.agents["LAT_ARB"] = la
        self.agent_latencies["LAT_ARB"] = latency_arb_latency(seed=300)

        # Human Player
        from simulation.agents.human_agent import HumanAgent

        class ZeroLatency:
            def sample_seconds(self) -> float:
                return 0.0

        self.human = HumanAgent(agent_id="HUMAN")
        self.agents["HUMAN"] = self.human
        # Zero simulated latency — human reflexes + network latency are already baked in
        self.agent_latencies["HUMAN"] = ZeroLatency()

    def set_ws_bridge(self, bridge):
        """Set the WebSocket bridge for bidirectional frontend communication."""
        self.ws_bridge = bridge

    def _process_incoming_ws_messages(self):
        """Poll incoming messages from the frontend and inject scenario events."""
        if not self.ws_bridge:
            return

        msgs = self.ws_bridge.poll_incoming()
        for raw_msg in msgs:
            try:
                data = json.loads(raw_msg)
                msg_type = data.get("type")

                if msg_type == "human_action":
                    action = data.get("action")
                    qty = int(data.get("qty", 100))
                    offset = int(data.get("offset", 0))

                    if action == "market_buy":
                        self.human.place_market_order(side=0, qty=qty)
                    elif action == "market_sell":
                        self.human.place_market_order(side=1, qty=qty)
                    elif action == "limit_buy":
                        self.human.place_limit_order(side=0, offset=offset, qty=qty)
                    elif action == "limit_sell":
                        self.human.place_limit_order(side=1, offset=offset, qty=qty)
                    elif action == "cancel_all":
                        self.human.cancel_all_quotes()

                    # Force the simulator to process the human's orders
                    from simulation.market.simulator_types import (Event,
                                                                   EventType)

                    self.event_queue.push(
                        Event(
                            timestamp=self.current_time,
                            event_type=EventType.AGENT_ACTION,
                            agent_id="HUMAN",
                        )
                    )

                elif msg_type == "scenario":
                    name = data.get("name")
                    log.info(f"Triggering interactive scenario: {name}")

                    if name == "flash_crash":
                        # Instant 100-tick drop
                        drop = 100
                        self.current_mid -= drop
                        if hasattr(self.price_process, "theta"):
                            self.price_process.theta -= drop
                        if hasattr(self.price_process, "current_price"):
                            self.price_process.current_price -= drop

                    elif name == "whale_buy":
                        # Massive market buy order
                        if self.tcp_client:
                            self.tcp_client.send_new_order(
                                order_id=999999,  # Magic ID for whale
                                side=0,  # BID (buy)
                                order_type=1,  # MARKET
                                price=0,
                                quantity=5000,
                            )

                    elif name == "vol_spike":
                        # Transition to volatile regime or manually spike OU sigma
                        if isinstance(self.price_process, RegimeSwitchingProcess):
                            self.price_process.current_regime = 1  # Volatile state
                        elif hasattr(self.price_process, "sigma"):
                            self.price_process.sigma *= 3.0  # Triples volatility

            except Exception as e:
                log.warning(f"Failed to process WS message: {e}")

    def _schedule_initial_events(self):
        """Schedule the initial burst of events that kicks off the simulation."""
        dt = self.config["simulation"].get("dt", 0.001)

        for step in range(self.total_steps):
            t = step * dt
            # Schedule mid-price updates.
            self.event_queue.push(
                Event(
                    timestamp=t,
                    event_type=EventType.MIDPRICE_MOVE,
                )
            )
            # Schedule agent actions (with per-agent latency offset).
            for agent_id, _agent in self.agents.items():
                latency_s = self.agent_latencies[agent_id].sample_seconds()
                self.event_queue.push(
                    Event(
                        timestamp=t + latency_s,
                        event_type=EventType.AGENT_ACTION,
                        agent_id=agent_id,
                    )
                )

            # Metrics sampling every 100 steps.
            if step % 100 == 0:
                self.event_queue.push(
                    Event(
                        timestamp=t + dt * 0.5,  # Half-step for metrics
                        event_type=EventType.METRICS_SAMPLE,
                    )
                )

    def _process_event(self, event: Event):
        """Process a single event from the queue."""

        if event.event_type == EventType.MIDPRICE_MOVE:
            self.current_mid = self.price_process.step()
            self.step_count += 1

            # Update informed agents with true price.
            for agent in self.agents.values():
                if hasattr(agent, "set_true_price"):
                    agent.set_true_price(self.current_mid)

            # Hawkes-driven additional order flow.
            if self.hawkes is not None:
                n_extra = self.hawkes.step(
                    dt=self.config["simulation"].get("dt", 0.001)
                )
                # Extra events manifest as noise trader actions.
                for _ in range(n_extra):
                    noise_id = "NOISE_0"
                    if noise_id in self.agents:
                        latency_s = self.agent_latencies[noise_id].sample_seconds()
                        self.event_queue.push(
                            Event(
                                timestamp=event.timestamp + latency_s,
                                event_type=EventType.AGENT_ACTION,
                                agent_id=noise_id,
                            )
                        )

        elif event.event_type == EventType.AGENT_ACTION:
            if self.last_book_update is None:
                return
            agent = self.agents.get(event.agent_id)
            if agent is None:
                return

            orders = agent.on_book_update(self.last_book_update, self.step_count)

            # Send cancels for stale quotes (cancel-and-replace pattern).
            if hasattr(agent, "_pending_cancels") and agent._pending_cancels:
                self._submit_cancels(agent._pending_cancels)
                agent._pending_cancels = []

            self._submit_orders(orders)

        elif event.event_type == EventType.METRICS_SAMPLE:
            mm = self.agents.get("MM")
            if mm:
                self.metrics.record_state(
                    step=self.step_count,
                    pnl=mm.stats.total_pnl,
                    inventory=mm.stats.inventory,
                    mid=self.current_mid,
                )

                if self.ws_bridge:
                    self.ws_bridge.push(
                        json.dumps(
                            {
                                "type": "metrics",
                                "pnl": mm.stats.total_pnl,
                                "inventory": mm.stats.inventory,
                                "mid": self.current_mid,
                            }
                        )
                    )

    def _submit_cancels(self, cancel_ids: list[int]):
        """Send cancel requests to the engine for stale quotes."""
        if self.tcp_client is None:
            return
        for order_id in cancel_ids:
            try:
                self.tcp_client.send_cancel(order_id)
                self.metrics.record_cancel(order_id, self.current_time)
            except Exception as e:
                log.warning(f"Failed to send cancel: {e}")

    def _submit_orders(self, orders: list[AgentOrder]):
        """Submit orders to the engine via TCP and update book snapshot."""
        if self.tcp_client is None:
            return

        for order in orders:
            try:
                self.tcp_client.send_new_order(
                    order_id=order.order_id,
                    side=order.side,
                    order_type=order.order_type,
                    price=order.price,
                    quantity=order.quantity,
                )
                self.metrics.record_quote(
                    QuoteRecord(
                        order_id=order.order_id,
                        timestamp=self.current_time,
                    )
                )
            except Exception as e:
                log.warning(f"Failed to send order: {e}")

        # Read back fills and book updates.
        try:
            messages = self.tcp_client.poll()
            for _msg_type, msg in messages:
                if isinstance(msg, BookUpdateMsg):
                    self.last_book_update = msg
                elif hasattr(msg, "maker_id"):
                    # Fill message — update all agents.
                    for agent in self.agents.values():
                        agent.on_fill(msg)

                    # Record fill for metrics.
                    mid_after = (
                        (
                            self.last_book_update.best_bid
                            + self.last_book_update.best_ask
                        )
                        // 2
                        if self.last_book_update
                        else self.current_mid
                    )

                    self.metrics.record_fill(
                        FillRecord(
                            timestamp=self.current_time,
                            price=msg.price,
                            quantity=msg.quantity,
                            side=0,  # Simplified
                            is_maker=True,
                            mid_at_fill=self.current_mid,
                            mid_after_fill=mid_after,
                            maker_fee=getattr(msg, "maker_fee", 0.0),
                            taker_fee=getattr(msg, "taker_fee", 0.0),
                        )
                    )

            if self.ws_bridge and self.last_book_update:
                book_msg = {
                    "type": "book",
                    "timestamp": self.current_time,
                    "bids": [
                        [p, q]
                        for p, q in zip(
                            self.last_book_update.bid_prices,
                            self.last_book_update.bid_quantities,
                            strict=False,
                        )
                    ],
                    "asks": [
                        [p, q]
                        for p, q in zip(
                            self.last_book_update.ask_prices,
                            self.last_book_update.ask_quantities,
                            strict=False,
                        )
                    ],
                }
                self.ws_bridge.push(json.dumps(book_msg))

        except Exception as e:
            log.warning(f"Failed to receive messages: {e}")

    def run(self):
        """Run the full simulation."""
        log.info("=" * 60)
        log.info("  LIQUIDITY ARENA — Event-Driven Simulator")
        log.info("=" * 60)

        # Create agents.
        self._create_agents()
        log.info(f"Created {len(self.agents)} agents: {list(self.agents.keys())}")

        # Create Hawkes process if configured.
        of_cfg = self.config.get("order_flow", {})
        if of_cfg.get("model") == "hawkes":
            self.hawkes = HawkesProcess(
                mu=of_cfg.get("hawkes_mu", 1.0),
                alpha=of_cfg.get("hawkes_alpha", 0.6),
                beta=of_cfg.get("hawkes_beta", 1.5),
            )
            log.info(
                f"Hawkes order flow enabled (branching ratio: {self.hawkes.branching_ratio:.2f})"
            )

        # Connect to engine.
        engine_cfg = self.config.get("engine", {})
        host = engine_cfg.get("host", "localhost")
        port = engine_cfg.get("port", 9876)

        try:
            self.tcp_client = TcpClient(host=host, port=port)
            self.tcp_client.connect()
            log.info(f"Connected to engine at {host}:{port}")
        except Exception as e:
            log.error(f"Failed to connect to engine: {e}")
            log.info("Running in standalone mode (no TCP, dry run)")
            self.tcp_client = None

        # Create an initial book update for agents.
        self.last_book_update = BookUpdateMsg(
            best_bid=self.current_mid - 5,
            best_ask=self.current_mid + 5,
            num_bid_levels=0,
            num_ask_levels=0,
            bid_prices=[],
            bid_quantities=[],
            ask_prices=[],
            ask_quantities=[],
        )

        # Schedule all events.
        self._schedule_initial_events()
        log.info(
            f"Scheduled {len(self.event_queue)} events for {self.total_steps} steps"
        )

        # Main event loop.
        self.running = True
        start_time = time.time()
        events_processed = 0

        while self.event_queue and self.running:
            if self.ws_bridge:
                self._process_incoming_ws_messages()

            event = self.event_queue.pop()
            self.current_time = event.timestamp
            self._process_event(event)
            events_processed += 1

            # Progress logging.
            if events_processed % 10000 == 0:
                elapsed = time.time() - start_time
                rate = events_processed / max(elapsed, 0.001)
                log.info(
                    f"  Step {self.step_count}/{self.total_steps} "
                    f"| {events_processed} events | {rate:.0f} events/sec"
                )

        elapsed = time.time() - start_time
        log.info(
            f"\nSimulation complete: {events_processed} events in {elapsed:.2f}s "
            f"({events_processed / max(elapsed, 0.001):.0f} events/sec)"
        )

        # Print metrics report.
        self.metrics.print_report()

        # Print per-agent stats.
        log.info("\nPer-Agent Statistics:")
        for agent_id, agent in self.agents.items():
            s = agent.stats
            log.info(
                f"  {agent_id:12s} | PnL: {s.total_pnl:8.0f} | Inv: {s.inventory:5d} "
                f"| Fills: {s.total_fills:4d} | Orders: {s.total_orders_sent:4d}"
            )

        # Cleanup.
        if self.tcp_client:
            self.tcp_client.disconnect()

    def stop(self):
        """Signal the simulation to stop."""
        self.running = False


def main():
    """Entry point."""
    import argparse

    parser = argparse.ArgumentParser(description="Liquidity Arena Simulator")
    parser.add_argument(
        "--config", default="simulation/config/default.yaml", help="Path to config YAML"
    )
    parser.add_argument(
        "--ws",
        action="store_true",
        help="Enable WebSocket bridge for browser dashboard",
    )
    parser.add_argument(
        "--ws-port", type=int, default=8765, help="WebSocket port (default: 8765)"
    )
    parser.add_argument(
        "--http-port",
        type=int,
        default=8080,
        help="HTTP static file port (default: 8080)",
    )
    args = parser.parse_args()

    sim = Simulator(config_path=args.config)

    # Wire up WebSocket bridge if requested.
    if args.ws:
        try:
            from simulation.market.ws_bridge import WebSocketBridge

            bridge = WebSocketBridge(
                ws_port=args.ws_port,
                http_port=args.http_port,
            )
            bridge.start()
            sim.set_ws_bridge(bridge)
            log.info(
                f"WebSocket bridge enabled — dashboard at http://localhost:{args.http_port}"
            )
        except ImportError:
            log.warning("websockets package not installed. Run: pip install websockets")
        except Exception as e:
            log.warning(f"Failed to start WebSocket bridge: {e}")

    # Graceful shutdown.
    def signal_handler(sig, frame):
        log.info("\nShutting down...")
        sim.stop()

    signal.signal(signal.SIGINT, signal_handler)

    sim.run()


if __name__ == "__main__":
    main()
