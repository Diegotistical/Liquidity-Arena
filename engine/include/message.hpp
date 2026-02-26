#pragma once
/// @file message.hpp
/// @brief Fixed-size binary message types for the TCP protocol.
///
/// All messages use a simple TLV (Type-Length-Value) framing:
///   [1 byte MsgType] [4 bytes payload length] [payload]
///
/// Using plain structs with memcpy serialization — no protobuf overhead.
/// This is intentional: in real HFT systems, custom binary protocols
/// outperform general-purpose serialization by 10-100x.

#include "types.hpp"
#include <cstdint>
#include <cstring>

namespace arena {

// ── Message types ────────────────────────────────────────────────────
enum class MsgType : uint8_t {
  NEW_ORDER = 1,
  CANCEL_ORDER = 2,
  FILL = 3,
  BOOK_UPDATE = 4,
  REJECT = 5,
  HEARTBEAT = 255
};

// ── Header (5 bytes) ─────────────────────────────────────────────────
struct MsgHeader {
  MsgType type;
  uint32_t length; // Payload length in bytes (excluding header)
} __attribute__((packed));

// ── Client → Engine messages ─────────────────────────────────────────

/// New order request from a client.
struct NewOrderMsg {
  static constexpr MsgType MSG_TYPE = MsgType::NEW_ORDER;

  OrderId id;
  Side side;
  OrderType type;
  Tick price; // In integer ticks (NEVER float)
  Quantity quantity;
} __attribute__((packed));

/// Cancel order request from a client.
struct CancelOrderMsg {
  static constexpr MsgType MSG_TYPE = MsgType::CANCEL_ORDER;

  OrderId id;
} __attribute__((packed));

// ── Engine → Client messages ─────────────────────────────────────────

/// Fill notification (sent to both maker and taker).
struct FillMsg {
  static constexpr MsgType MSG_TYPE = MsgType::FILL;

  OrderId maker_id;
  OrderId taker_id;
  Tick price;
  Quantity quantity;
} __attribute__((packed));

/// Order rejection.
struct RejectMsg {
  static constexpr MsgType MSG_TYPE = MsgType::REJECT;

  OrderId id;
  uint8_t reason; // 0=invalid, 1=pool_full, 2=cancel_not_found
} __attribute__((packed));

/// LOB snapshot (top N levels, broadcast after each event).
static constexpr int MAX_DEPTH_LEVELS = 10;

struct BookUpdateMsg {
  static constexpr MsgType MSG_TYPE = MsgType::BOOK_UPDATE;

  Tick best_bid;
  Tick best_ask;
  int32_t num_bid_levels;
  int32_t num_ask_levels;
  Tick bid_prices[MAX_DEPTH_LEVELS];
  Quantity bid_quantities[MAX_DEPTH_LEVELS];
  Tick ask_prices[MAX_DEPTH_LEVELS];
  Quantity ask_quantities[MAX_DEPTH_LEVELS];
} __attribute__((packed));

// ── Serialization helpers ────────────────────────────────────────────

/// Serialize a message into a buffer with header. Returns total bytes written.
/// Buffer must be large enough: sizeof(MsgHeader) + sizeof(MsgT).
template <typename MsgT>
std::size_t serialize(const MsgT &msg, uint8_t *buffer) {
  MsgHeader header;
  header.type = MsgT::MSG_TYPE;
  header.length = static_cast<uint32_t>(sizeof(MsgT));

  std::memcpy(buffer, &header, sizeof(MsgHeader));
  std::memcpy(buffer + sizeof(MsgHeader), &msg, sizeof(MsgT));
  return sizeof(MsgHeader) + sizeof(MsgT);
}

/// Deserialize a message from a buffer (after header has been read).
template <typename MsgT> MsgT deserialize(const uint8_t *payload) {
  MsgT msg;
  std::memcpy(&msg, payload, sizeof(MsgT));
  return msg;
}

/// Maximum possible message size (for buffer allocation).
constexpr std::size_t MAX_MSG_SIZE = sizeof(MsgHeader) + sizeof(BookUpdateMsg);

} // namespace arena
