#pragma once
/// @file order.hpp
/// @brief Order struct optimized for cache locality.
///
/// Designed to fit within 2 cache lines (128 bytes).
/// Uses intrusive doubly-linked list pointers for O(1) PriceLevel operations.
///
/// Design notes:
///   - Hot fields (id, price, quantity, side) are in the first 32 bytes
///   - Intrusive list pointers are adjacent for prefetch-friendly traversal
///   - Iceberg fields (display_qty, hidden_qty) support hidden liquidity

#include "types.hpp"

namespace arena {

/// A single order in the limit order book.
/// Layout is optimized for cache-line alignment:
///   - Hot fields (id, price, quantity, side) are in the first 32 bytes
///   - Intrusive list pointers are adjacent for prefetch-friendly traversal
///   - Iceberg fields enable hidden liquidity simulation
struct Order {
  OrderId id = INVALID_ORDER;
  Tick price = INVALID_PRICE;
  Quantity quantity = 0;
  Quantity filled_quantity = 0;
  Side side = Side::BID;
  OrderType type = OrderType::LIMIT;
  Timestamp timestamp = 0;

  // ── Iceberg order fields ─────────────────────────────────────────
  /// For iceberg orders: the visible portion shown to the market.
  /// For regular orders: display_qty == quantity (everything is visible).
  Quantity display_qty = 0;
  /// For iceberg orders: remaining hidden quantity that replenishes display.
  Quantity hidden_qty = 0;

  // ── Intrusive doubly-linked list pointers for PriceLevel ─────────
  // Using raw pointers because these point into the ObjectPool's
  // pre-allocated storage — no ownership semantics needed.
  Order *prev = nullptr;
  Order *next = nullptr;

  /// Remaining quantity available to fill (display portion for icebergs).
  [[nodiscard]] constexpr Quantity remaining() const noexcept { return quantity - filled_quantity; }

  /// Visible remaining quantity (what other participants see in the book).
  /// For regular orders: same as remaining().
  /// For icebergs: min(display_qty, remaining()) — only the display slice.
  [[nodiscard]] constexpr Quantity visible_remaining() const noexcept {
    if (type == OrderType::ICEBERG && display_qty > 0) {
      Quantity rem = remaining();
      return (rem < display_qty) ? rem : display_qty;
    }
    return remaining();
  }

  /// Whether this order has been fully filled.
  [[nodiscard]] constexpr bool is_filled() const noexcept { return filled_quantity >= quantity; }

  /// Whether this is an iceberg order with hidden quantity remaining.
  [[nodiscard]] constexpr bool has_hidden_qty() const noexcept {
    return type == OrderType::ICEBERG && hidden_qty > 0;
  }

  /// Reset all fields for reuse from the object pool.
  void reset() noexcept {
    id = INVALID_ORDER;
    price = INVALID_PRICE;
    quantity = 0;
    filled_quantity = 0;
    side = Side::BID;
    type = OrderType::LIMIT;
    timestamp = 0;
    display_qty = 0;
    hidden_qty = 0;
    prev = nullptr;
    next = nullptr;
  }
};

} // namespace arena
