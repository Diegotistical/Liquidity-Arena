"""
ws_bridge.py — WebSocket bridge + HTTP static file server.

Pushes real-time LOB snapshots and PnL data from the simulator
to connected browser clients via WebSocket (JSON).
Also serves the frontend/ directory as static files.
"""

import asyncio
import http.server
import socketserver
import threading
from pathlib import Path
from typing import Set

import websockets


class WebSocketBridge:
    """
    Async WebSocket server that broadcasts simulation data to browsers.
    Also serves the frontend static files via a simple HTTP server.
    """

    def __init__(
        self, ws_port: int = 8765, http_port: int = 8080, frontend_dir: str = "frontend"
    ):
        self.ws_port = ws_port
        self.http_port = http_port
        self.frontend_dir = Path(frontend_dir).resolve()
        self._clients: Set = set()
        self._message_queue: asyncio.Queue = asyncio.Queue()
        self._loop: asyncio.AbstractEventLoop = None
        self._ws_thread: threading.Thread = None
        self._http_thread: threading.Thread = None

    def start(self):
        """Start both WebSocket and HTTP servers in background threads."""
        # Start HTTP server for static files.
        self._http_thread = threading.Thread(target=self._run_http, daemon=True)
        self._http_thread.start()

        # Start WebSocket server.
        self._ws_thread = threading.Thread(target=self._run_ws, daemon=True)
        self._ws_thread.start()

        print(f"[WS] WebSocket server on ws://localhost:{self.ws_port}")
        print(f"[HTTP] Frontend at http://localhost:{self.http_port}")

    def push(self, data: str):
        """Push data to all connected WebSocket clients (thread-safe)."""
        if self._loop is not None:
            asyncio.run_coroutine_threadsafe(self._message_queue.put(data), self._loop)

    def _run_http(self):
        """Run a simple HTTP server for the frontend."""
        import functools

        handler = functools.partial(
            http.server.SimpleHTTPRequestHandler, directory=str(self.frontend_dir)
        )
        with socketserver.TCPServer(("", self.http_port), handler) as httpd:
            httpd.serve_forever()

    def _run_ws(self):
        """Run the async WebSocket server."""
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        self._loop.run_until_complete(self._ws_main())

    async def _ws_main(self):
        """WebSocket main loop."""
        async with websockets.serve(self._ws_handler, "0.0.0.0", self.ws_port):
            # Broadcast messages from the queue.
            while True:
                data = await self._message_queue.get()
                if self._clients:
                    await asyncio.gather(
                        *[client.send(data) for client in self._clients],
                        return_exceptions=True,
                    )

    async def _ws_handler(self, websocket):
        """Handle a new WebSocket connection."""
        self._clients.add(websocket)
        print(f"[WS] Client connected (total: {len(self._clients)})")
        try:
            async for _ in websocket:
                pass  # Read-only: we don't expect messages from the browser
        finally:
            self._clients.discard(websocket)
            print(f"[WS] Client disconnected (total: {len(self._clients)})")
