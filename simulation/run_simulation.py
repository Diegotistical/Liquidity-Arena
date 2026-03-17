"""
run_simulation.py — Entry point for the simulation.

Usage:
    1. Start the C++ engine:  ./build/arena_engine.exe
    2. Run this script:       python -m simulation.run_simulation
    3. Open browser:          http://localhost:8080
"""

import logging
import os
import sys

# Add project root to path.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from simulation.market.ws_bridge import WebSocketBridge  # noqa: E402
from simulation.simulator import Simulator  # noqa: E402

# Configure logging.
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger(__name__)


def main():
    config_path = "simulation/config/default.yaml"
    if len(sys.argv) > 1:
        config_path = sys.argv[1]

    # Start WebSocket bridge + HTTP server.
    bridge = WebSocketBridge(ws_port=8765, http_port=8080, frontend_dir="frontend")
    bridge.start()
    log.info("WebSocket bridge started on ws://localhost:8765")
    log.info("Frontend served at http://localhost:8080")

    # Create and run the simulation.
    sim = Simulator(config_path)
    sim.set_ws_bridge(bridge)

    try:
        results = sim.run()
        log.info("Results: %d steps recorded.", len(results))
    except KeyboardInterrupt:
        log.info("Interrupted by user.")
    except ConnectionRefusedError:
        log.error("Could not connect to the C++ engine.")
        log.error("Make sure to start the engine first: ./build/arena_engine.exe")
        sys.exit(1)


if __name__ == "__main__":
    main()
