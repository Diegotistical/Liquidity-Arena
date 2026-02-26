"""
simulator.py — Main simulation orchestrator.

Connects all agents to the C++ engine via TCP,
runs the simulation loop, and pushes data to the frontend
via WebSocket bridge.
"""

import json
import logging
import signal
import sys
import time
from pathlib import Path
from typing import Optional

import yaml

from simulation.market.tcp_client import TcpClient, BookUpdateMsg, FillMsg, MsgType
from simulation.market.price_process import OUProcess, GBMProcess
from simulation.agents.market_maker import AvellanedaStoikovMM
from simulation.agents.noise_trader import NoiseTrader
from simulation.agents.informed_trader import InformedTrader
from simulation.agents.momentum_trader import MomentumTrader

log = logging.getLogger(__name__)


class Simulator:
    """
    Orchestrates the simulation:
    1. Generates the true price path
    2. Creates all agents
    3. Each step: update price → agents react → send orders → collect fills
    4. Pushes LOB + PnL data to the WebSocket bridge for frontend
    """

    def __init__(self, config_path: str = "simulation/config/default.yaml"):
        with open(config_path) as f:
            self.config = yaml.safe_load(f)

        self.num_steps = self.config['simulation']['num_steps']
        self.step_interval = self.config['simulation']['step_interval_ms'] / 1000.0

        # True price process.
        pp_cfg = self.config['price_process']
        if pp_cfg['type'] == 'OU':
            self.price_process = OUProcess(
                initial_price=pp_cfg['initial_price'],
                kappa=pp_cfg['kappa'],
                theta=pp_cfg['theta'],
                sigma=pp_cfg['sigma'],
                seed=pp_cfg.get('seed', 42)
            )
        else:
            self.price_process = GBMProcess(
                initial_price=pp_cfg['initial_price'],
                sigma=pp_cfg['sigma'],
                seed=pp_cfg.get('seed', 42)
            )

        self.tick_size = self.config['engine']['tick_size']

        # Create agents.
        self._create_agents()

        # TCP client for the simulation.
        self.client = TcpClient(
            host=self.config['engine']['host'],
            port=self.config['engine']['port']
        )

        # Latest book update.
        self.latest_book: Optional[BookUpdateMsg] = None
        self.results: list[dict] = []

        # WebSocket callback for pushing data to frontend.
        self._ws_callback = None

        # Graceful shutdown.
        self._running = True
        signal.signal(signal.SIGINT, self._handle_signal)
        signal.signal(signal.SIGTERM, self._handle_signal)

    def _handle_signal(self, signum: int, frame) -> None:
        log.info("Received signal %d, shutting down gracefully...", signum)
        self._running = False

    def _create_agents(self):
        """Instantiate all agents from config."""
        self.agents = []

        # Market maker (the star).
        mm_cfg = self.config['agents']['market_maker']
        self.market_maker = AvellanedaStoikovMM(
            agent_id="MM",
            gamma=mm_cfg['gamma'],
            kappa=mm_cfg['kappa'],
            max_inventory=mm_cfg['max_inventory'],
            num_levels=mm_cfg['num_levels'],
            level_spacing=mm_cfg['level_spacing'],
            base_quantity=mm_cfg['base_quantity'],
            sigma_window=mm_cfg['sigma_window'],
            total_steps=self.num_steps,
            tick_size=self.tick_size
        )
        self.agents.append(self.market_maker)

        # Noise traders.
        nt_cfg = self.config['agents']['noise_traders']
        for i in range(nt_cfg['count']):
            trader = NoiseTrader(
                agent_id=f"NOISE_{i}",
                order_prob=nt_cfg['order_prob'],
                spread_range=nt_cfg['spread_range'],
                min_qty=nt_cfg['min_qty'],
                max_qty=nt_cfg['max_qty'],
                market_order_prob=nt_cfg['market_order_prob'],
                seed=42 + i + 100,
                tick_size=self.tick_size
            )
            self.agents.append(trader)

        # Informed traders.
        it_cfg = self.config['agents']['informed_traders']
        self.informed_traders = []
        for i in range(it_cfg['count']):
            trader = InformedTrader(
                agent_id=f"INFORMED_{i}",
                threshold=it_cfg['threshold'],
                aggression=it_cfg['aggression'],
                min_qty=it_cfg['min_qty'],
                max_qty=it_cfg['max_qty'],
                seed=42 + i + 200,
                tick_size=self.tick_size
            )
            self.agents.append(trader)
            self.informed_traders.append(trader)

        # Momentum traders.
        mt_cfg = self.config['agents']['momentum_traders']
        for i in range(mt_cfg['count']):
            trader = MomentumTrader(
                agent_id=f"MOMENTUM_{i}",
                fast_period=mt_cfg['fast_period'],
                slow_period=mt_cfg['slow_period'],
                trade_prob=mt_cfg['trade_prob'],
                min_qty=mt_cfg['min_qty'],
                max_qty=mt_cfg['max_qty'],
                seed=42 + i + 300,
                tick_size=self.tick_size
            )
            self.agents.append(trader)

    def set_ws_callback(self, callback):
        """Set a callback to push data to the WebSocket bridge."""
        self._ws_callback = callback

    def _seed_book(self):
        """Place initial orders to bootstrap the order book."""
        initial_price = self.config['price_process']['initial_price']

        # Seed with orders around the initial price.
        for i in range(1, 11):
            # Bids below initial price.
            self.client.send_new_order(
                order_id=900000 + i,
                side=0,  # BID
                order_type=0,  # LIMIT
                price=initial_price - i * 2,
                quantity=100
            )
            time.sleep(0.001)

            # Asks above initial price.
            self.client.send_new_order(
                order_id=900100 + i,
                side=1,  # ASK
                order_type=0,  # LIMIT
                price=initial_price + i * 2,
                quantity=100
            )
            time.sleep(0.001)

        # Give the engine a moment to process.
        time.sleep(0.05)

    def run(self):
        """Run the full simulation."""
        log.info("=" * 60)
        log.info("  LIQUIDITY ARENA SIMULATION")
        log.info("  Steps: %d | Agents: %d", self.num_steps, len(self.agents))
        log.info("  Price process: %s", self.config['price_process']['type'])
        log.info("=" * 60)

        # Connect to the C++ engine.
        try:
            self.client.connect()
        except Exception as e:
            log.error("Failed to connect to engine: %s", e)
            sys.exit(1)
        log.info("Connected to engine")

        # Seed the order book.
        self._seed_book()

        # Read initial book state.
        time.sleep(0.1)
        messages = self.client.poll()
        for msg_type, msg in messages:
            if msg_type == MsgType.BOOK_UPDATE:
                self.latest_book = msg

        update_interval = self.config.get('visualization', {}).get('update_interval', 5)

        for step in range(self.num_steps):
            if not self._running:
                log.info("Interrupted at step %d", step)
                break

            # Generate true price.
            true_price = self.price_process.step()

            # Update informed traders with the true price.
            for it in self.informed_traders:
                it.set_true_price(true_price)

            # Each agent reacts to the latest book update.
            if self.latest_book is not None:
                for agent in self.agents:
                    try:
                        orders = agent.on_book_update(self.latest_book, step)
                        for order in orders:
                            self.client.send_new_order(
                                order_id=order.order_id,
                                side=order.side,
                                order_type=order.order_type,
                                price=order.price,
                                quantity=order.quantity
                            )
                    except Exception as e:
                        log.warning("Agent %s error: %s", agent.agent_id, e)

            # Poll for responses (fills + book updates).
            time.sleep(self.step_interval)
            try:
                messages = self.client.poll()
            except Exception as e:
                log.error("TCP poll error: %s", e)
                break

            for msg_type, msg in messages:
                if msg_type == MsgType.BOOK_UPDATE:
                    self.latest_book = msg
                elif msg_type == MsgType.FILL:
                    # Route fills to the correct agent.
                    for agent in self.agents:
                        agent.on_fill(msg, msg.taker_id)
                        agent.on_fill(msg, msg.maker_id)

            # Record step data.
            if self.latest_book:
                mid = (self.latest_book.best_bid + self.latest_book.best_ask) // 2
                for agent in self.agents:
                    agent.update_unrealized_pnl(mid)

            step_data = {
                'step': step,
                'true_price': true_price,
                'mm_pnl': self.market_maker.stats.total_pnl,
                'mm_inventory': self.market_maker.stats.inventory,
                'mm_fills': self.market_maker.stats.total_fills,
                'spread': (self.latest_book.best_ask - self.latest_book.best_bid)
                          if self.latest_book else 0,
                'best_bid': self.latest_book.best_bid if self.latest_book else 0,
                'best_ask': self.latest_book.best_ask if self.latest_book else 0,
            }
            self.results.append(step_data)

            # Push to WebSocket for frontend.
            if self._ws_callback and step % update_interval == 0:
                ws_data = {
                    'type': 'update',
                    'step': step,
                    'book': {
                        'best_bid': self.latest_book.best_bid if self.latest_book else 0,
                        'best_ask': self.latest_book.best_ask if self.latest_book else 0,
                        'bid_prices': self.latest_book.bid_prices[:self.latest_book.num_bid_levels] if self.latest_book else [],
                        'bid_quantities': self.latest_book.bid_quantities[:self.latest_book.num_bid_levels] if self.latest_book else [],
                        'ask_prices': self.latest_book.ask_prices[:self.latest_book.num_ask_levels] if self.latest_book else [],
                        'ask_quantities': self.latest_book.ask_quantities[:self.latest_book.num_ask_levels] if self.latest_book else [],
                    },
                    'mm': {
                        'pnl': self.market_maker.stats.total_pnl,
                        'inventory': self.market_maker.stats.inventory,
                        'fills': self.market_maker.stats.total_fills,
                        'spread': step_data['spread'],
                    },
                    'mm_model': self._get_model_params(step),
                }
                self._ws_callback(json.dumps(ws_data))

            # Progress logging.
            if step % 1000 == 0:
                log.info("Step %5d/%d | MM PnL: %8.2f | Inv: %4d | Fills: %4d | Spread: %3d",
                         step, self.num_steps,
                         self.market_maker.stats.total_pnl,
                         self.market_maker.stats.inventory,
                         self.market_maker.stats.total_fills,
                         step_data['spread'])

        self.client.disconnect()
        self._print_summary()
        return self.results

    def _get_model_params(self, step: int) -> dict:
        """Extract A-S model parameters from the market maker for the frontend."""
        mm = self.market_maker
        mid = 0
        if self.latest_book:
            mid = (self.latest_book.best_bid + self.latest_book.best_ask) // 2

        # Compute reservation price: r = s - q * gamma * sigma^2 * (T - t)
        T_minus_t = max(0, (self.num_steps - step) / self.num_steps)
        sigma_sq = getattr(mm, '_last_sigma_sq', 0)
        reservation = mid - mm.stats.inventory * mm._gamma * sigma_sq * T_minus_t

        return {
            'reservation': int(reservation) if reservation else 0,
            'sigma_sq': sigma_sq,
            'spread': mm._gamma * sigma_sq * T_minus_t + (2 / mm._gamma) * 1.0 if mm._gamma > 0 else 0,
        }

    def _print_summary(self):
        """Print final simulation summary."""
        mm = self.market_maker
        log.info("=" * 60)
        log.info("  SIMULATION COMPLETE")
        log.info("=" * 60)
        log.info("  Market Maker (%s):", mm.agent_id)
        log.info("    Total PnL:      $%10.2f", mm.stats.total_pnl)
        log.info("    Realized PnL:   $%10.2f", mm.stats.realized_pnl)
        log.info("    Unrealized PnL: $%10.2f", mm.stats.unrealized_pnl)
        log.info("    Final Inventory: %5d", mm.stats.inventory)
        log.info("    Total Fills:     %5d", mm.stats.total_fills)
        log.info("    Total Volume:    %5d", mm.stats.total_volume)
        log.info("=" * 60)
