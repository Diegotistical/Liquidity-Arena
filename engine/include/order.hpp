#pragma once
/// @file order.hpp
/// @brief Order struct optimized for cache locality.
/// Designed to fit within 2 cache lines (128 bytes).
/// Uses intrusive doubly-linked list pointers for O(1) PriceLevel operations.

#include "types.hpp"

namespace arena {

/// A single order in the limit order book.
/// Layout is optimized for cache-line alignment:
///   - Hot fields (id, price, quantity, side) are in the first 32 bytes
///   - Intrusive list pointers are adjacent for prefetch-friendly traversal
struct Order {
  OrderId id = INVALID_ORDER;
  Tick price = INVALID_PRICE;
  Quantity quantity = 0;
  Quantity filled_quantity = 0;
  Side side = Side::BID;
  OrderType type = OrderType::LIMIT;
  Timestamp timestamp = 0;

  // Intrusive doubly-linked list pointers for PriceLevel.
  // Using raw pointers because these point into the ObjectPool's
  // pre-allocated storage — no ownership semantics needed.
  Order *prev = nullptr;
  Order *next = nullptr;

  /// Remaining quantity available to fill.
  [[nodiscard]] constexpr Quantity remaining() const noexcept {
    return quantity - filled_quantity;
  }

  /// Whether this order has been fully filled.
  [[nodiscard]] constexpr bool is_filled() const noexcept {
    return filled_quantity >= quantity;
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
    prev = nullptr;
    next = nullptr;
  }
};

} // namespace arena
