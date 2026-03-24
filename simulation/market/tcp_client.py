"""
tcp_client.py — Python TCP client for the Liquidity Arena matching engine.

Matches the C++ binary protocol exactly:
  Header: [1-byte MsgType] [4-byte payload length (LE)]
  Payload: packed struct fields

Uses Python's struct module for zero-dependency binary packing.
"""

import socket
import struct
from collections.abc import Callable
from dataclasses import dataclass, field
from enum import IntEnum


class MsgType(IntEnum):
    NEW_ORDER = 1
    CANCEL_ORDER = 2
    FILL = 3
    BOOK_UPDATE = 4
    REJECT = 5
    HEARTBEAT = 255


# ── Message structs ───────────────────────────────────────────────────


@dataclass
class NewOrderMsg:
    """Client → Engine: New order request."""

    id: int
    side: int  # 0=BID, 1=ASK
    type: int  # 0=LIMIT, 1=MARKET, 2=ICEBERG
    price: int  # Integer ticks
    quantity: int
    display_qty: int = 0  # For iceberg: visible portion. 0 = full visibility.

    # Packed format: uint64 OrderId, uint8 Side, uint8 Type, int64 Price, int32 Qty, int32 DisplayQty
    FORMAT = "<QBBqii"
    SIZE = struct.calcsize(FORMAT)

    def pack(self) -> bytes:
        return struct.pack(
            self.FORMAT,
            self.id,
            self.side,
            self.type,
            self.price,
            self.quantity,
            self.display_qty,
        )


@dataclass
class CancelOrderMsg:
    """Client → Engine: Cancel order request."""

    id: int

    FORMAT = "<Q"
    SIZE = struct.calcsize(FORMAT)

    def pack(self) -> bytes:
        return struct.pack(self.FORMAT, self.id)


@dataclass
class FillMsg:
    """Engine → Client: Fill notification."""

    maker_id: int
    taker_id: int
    price: int
    quantity: int

    FORMAT = "<QQqi"
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "FillMsg":
        maker_id, taker_id, price, qty = struct.unpack(cls.FORMAT, data[: cls.SIZE])
        return cls(maker_id, taker_id, price, qty)


MAX_DEPTH_LEVELS = 10


@dataclass
class BookUpdateMsg:
    """Engine → Client: LOB snapshot."""

    best_bid: int = 0
    best_ask: int = 0
    num_bid_levels: int = 0
    num_ask_levels: int = 0
    bid_prices: list = field(default_factory=lambda: [0] * MAX_DEPTH_LEVELS)
    bid_quantities: list = field(default_factory=lambda: [0] * MAX_DEPTH_LEVELS)
    ask_prices: list = field(default_factory=lambda: [0] * MAX_DEPTH_LEVELS)
    ask_quantities: list = field(default_factory=lambda: [0] * MAX_DEPTH_LEVELS)

    # q=best_bid, q=best_ask, i=num_bid, i=num_ask, 10q bids, 10i bidqty, 10q asks, 10i askqty
    FORMAT = "<qqii" + "q" * 10 + "i" * 10 + "q" * 10 + "i" * 10
    SIZE = struct.calcsize(FORMAT)

    @classmethod
    def unpack(cls, data: bytes) -> "BookUpdateMsg":
        values = struct.unpack(cls.FORMAT, data[: cls.SIZE])
        msg = cls()
        msg.best_bid = values[0]
        msg.best_ask = values[1]
        msg.num_bid_levels = values[2]
        msg.num_ask_levels = values[3]
        msg.bid_prices = list(values[4:14])
        msg.bid_quantities = list(values[14:24])
        msg.ask_prices = list(values[24:34])
        msg.ask_quantities = list(values[34:44])
        return msg


# ── Header ────────────────────────────────────────────────────────────

HEADER_FORMAT = "<BI"  # 1-byte type + 4-byte length
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)


class TcpClient:
    """TCP client that communicates with the C++ matching engine."""

    def __init__(self, host: str = "127.0.0.1", port: int = 9876):
        self.host = host
        self.port = port
        self.sock: socket.socket | None = None
        self._recv_buffer = bytearray()
        self._on_fill: Callable | None = None
        self._on_book_update: Callable | None = None

    def connect(self):
        """Connect to the engine."""
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.connect((self.host, self.port))
        self.sock.setblocking(False)

    def disconnect(self):
        """Close the connection."""
        if self.sock:
            self.sock.close()
            self.sock = None

    def set_fill_callback(self, cb: Callable):
        self._on_fill = cb

    def set_book_update_callback(self, cb: Callable):
        self._on_book_update = cb

    def send_new_order(
        self,
        order_id: int,
        side: int,
        order_type: int,
        price: int,
        quantity: int,
        display_qty: int = 0,
    ):
        """Send a new order to the engine."""
        msg = NewOrderMsg(order_id, side, order_type, price, quantity, display_qty)
        payload = msg.pack()
        header = struct.pack(HEADER_FORMAT, MsgType.NEW_ORDER, len(payload))
        self.sock.sendall(header + payload)

    def send_cancel(self, order_id: int):
        """Send a cancel request to the engine."""
        msg = CancelOrderMsg(order_id)
        payload = msg.pack()
        header = struct.pack(HEADER_FORMAT, MsgType.CANCEL_ORDER, len(payload))
        self.sock.sendall(header + payload)

    def poll(self, timeout: float = 0.01) -> list:
        """
        Read and process available messages from the engine.
        Returns a list of (MsgType, message) tuples.
        """
        messages = []

        try:
            data = self.sock.recv(4096)
            if not data:
                return messages
            self._recv_buffer.extend(data)
        except BlockingIOError:
            pass
        except OSError:
            return messages

        # Process complete messages from buffer.
        while len(self._recv_buffer) >= HEADER_SIZE:
            msg_type, payload_len = struct.unpack(
                HEADER_FORMAT, self._recv_buffer[:HEADER_SIZE]
            )

            total_size = HEADER_SIZE + payload_len
            if len(self._recv_buffer) < total_size:
                break  # Incomplete message

            payload = bytes(self._recv_buffer[HEADER_SIZE:total_size])
            self._recv_buffer = self._recv_buffer[total_size:]

            msg_type_enum = MsgType(msg_type)

            if msg_type_enum == MsgType.FILL:
                fill = FillMsg.unpack(payload)
                messages.append((msg_type_enum, fill))
                if self._on_fill:
                    self._on_fill(fill)

            elif msg_type_enum == MsgType.BOOK_UPDATE:
                update = BookUpdateMsg.unpack(payload)
                messages.append((msg_type_enum, update))
                if self._on_book_update:
                    self._on_book_update(update)

        return messages
