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
///
/// Portability: Uses ARENA_PACKED macros for cross-platform struct packing
/// (GCC/Clang __attribute__((packed)) vs MSVC #pragma pack).

#include "types.hpp"

#include <cstdint>
#include <cstring>


// ── Cross-platform struct packing ────────────────────────────────────
// MSVC uses #pragma pack; GCC/Clang use __attribute__((packed)).
// Usage: ARENA_PACK_BEGIN struct Foo { ... } ARENA_PACK_END;
#ifdef _MSC_VER
#define ARENA_PACK_BEGIN __pragma(pack(push, 1))
#define ARENA_PACK_END __pragma(pack(pop))
#define ARENA_PACKED
#else
#define ARENA_PACK_BEGIN
#define ARENA_PACK_END
#define ARENA_PACKED __attribute__((packed))
#endif

namespace arena {

// ── Message types ────────────────────────────────────────────────────
enum class MsgType : uint8_t {
  NEW_ORDER = 1,
  CANCEL_ORDER = 2,
  FILL = 3,
  BOOK_UPDATE = 4,
  REJECT = 5,
  MODIFY_ORDER = 6,
  HEARTBEAT = 255
};

// ── Header (5 bytes) ─────────────────────────────────────────────────
ARENA_PACK_BEGIN
struct MsgHeader {
  MsgType type;
  uint32_t length; // Payload length in bytes (excluding header)
} ARENA_PACKED;
ARENA_PACK_END

// ── Client → Engine messages ─────────────────────────────────────────

/// New order request from a client.
ARENA_PACK_BEGIN
struct NewOrderMsg {
  static constexpr MsgType MSG_TYPE = MsgType::NEW_ORDER;

  OrderId id;
  Side side;
  OrderType type;
  Tick price; // In integer ticks (NEVER float)
  Quantity quantity;
  Quantity display_qty; // For iceberg: visible portion. 0 = full visibility.
} ARENA_PACKED;
ARENA_PACK_END

/// Cancel order request from a client.
ARENA_PACK_BEGIN
struct CancelOrderMsg {
  static constexpr MsgType MSG_TYPE = MsgType::CANCEL_ORDER;

  OrderId id;
} ARENA_PACKED;
ARENA_PACK_END

/// Modify order request from a client.
/// If new_price differs from current, the order loses queue position (cancel+re-add).
/// If only quantity decreases, queue position is preserved.
ARENA_PACK_BEGIN
struct ModifyOrderMsg {
  static constexpr MsgType MSG_TYPE = MsgType::MODIFY_ORDER;

  OrderId id;
  Tick new_price;
  Quantity new_quantity;
} ARENA_PACKED;
ARENA_PACK_END

// ── Engine → Client messages ─────────────────────────────────────────

/// Fill notification (sent to both maker and taker).
ARENA_PACK_BEGIN
struct FillMsg {
  static constexpr MsgType MSG_TYPE = MsgType::FILL;

  OrderId maker_id;
  OrderId taker_id;
  Tick price;
  Quantity quantity;
  double maker_fee; // Rebate earned by maker (negative = earned)
  double taker_fee; // Fee paid by taker (positive = paid)
} ARENA_PACKED;
ARENA_PACK_END

/// Order rejection.
ARENA_PACK_BEGIN
struct RejectMsg {
  static constexpr MsgType MSG_TYPE = MsgType::REJECT;

  OrderId id;
  RejectReason reason;
} ARENA_PACKED;
ARENA_PACK_END

/// LOB snapshot (top N levels, broadcast after each event).
static constexpr int MAX_DEPTH_LEVELS = 10;

ARENA_PACK_BEGIN
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
} ARENA_PACKED;
ARENA_PACK_END

// ── Serialization helpers ────────────────────────────────────────────

/// Serialize a message into a buffer with header. Returns total bytes written.
/// Buffer must be large enough: sizeof(MsgHeader) + sizeof(MsgT).
template <typename MsgT>
std::size_t serialize(const MsgT& msg, uint8_t *buffer) {
  MsgHeader header;
  header.type = MsgT::MSG_TYPE;
  header.length = static_cast<uint32_t>(sizeof(MsgT));

  std::memcpy(buffer, &header, sizeof(MsgHeader));
  std::memcpy(buffer + sizeof(MsgHeader), &msg, sizeof(MsgT));
  return sizeof(MsgHeader) + sizeof(MsgT);
}

/// Deserialize a message from a buffer (after header has been read).
template <typename MsgT>
MsgT deserialize(const uint8_t *payload) {
  MsgT msg;
  std::memcpy(&msg, payload, sizeof(MsgT));
  return msg;
}

/// Maximum possible message size (for buffer allocation).
constexpr std::size_t MAX_MSG_SIZE = sizeof(MsgHeader) + sizeof(BookUpdateMsg);

} // namespace arena
