#pragma once
/// @file order_book.hpp
/// @brief Limit Order Book with O(1) amortized operations.
///
/// Architecture:
///   - Two unordered_maps (bids, asks): Tick → PriceLevel
///   - ObjectPool for zero-allocation order creation
///   - Tracks best bid/ask for O(1) BBO queries
///   - get_depth() returns top-N levels for the frontend
///   - Supports order modify (price change = cancel+re-add, qty down = in-place)
///   - Supports iceberg orders (hidden liquidity with display portion)
///
/// Complexity:
///   - add_order:    O(1) amortized (hash map insert + list append)
///   - cancel_order: O(1) (hash map lookup + list remove)
///   - modify_order: O(1) for qty down, O(1) amortized for price change
///   - get_bbo:      O(1)
///   - get_depth:    O(N·log N) where N = number of active price levels

#include "object_pool.hpp"
#include "order.hpp"
#include "price_level.hpp"
#include "types.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <unordered_map>
#include <vector>

namespace arena {

/// Maximum orders the pool can hold simultaneously.
constexpr std::size_t MAX_ORDERS = 1'000'000;

/// A depth level for external queries.
struct DepthLevel {
  Tick price;
  Quantity quantity;
};

/// The Limit Order Book.
class OrderBook {
public:
  OrderBook() = default;

  // Non-copyable (owns the object pool).
  OrderBook(const OrderBook&) = delete;
  OrderBook& operator=(const OrderBook&) = delete;

  /// Add a limit order to the book. Returns the Order pointer (owned by pool).
  /// Returns nullptr if the pool is exhausted.
  Order *add_order(OrderId id, Side side, Tick price, Quantity quantity, Timestamp ts);

  /// Add an iceberg order. display_qty is the visible portion; the rest is
  /// hidden.
  Order *add_iceberg_order(OrderId id, Side side, Tick price, Quantity total_qty,
                           Quantity display_qty, Timestamp ts);

  /// Cancel and remove an order by ID. Returns true if found and removed.
  bool cancel_order(OrderId id);

  /// Modify an order's price and/or quantity.
  /// - If price changes: cancel + re-add (loses queue position).
  /// - If only quantity decreases: in-place modify (keeps queue position).
  /// - Quantity increase or other invalid changes return false.
  /// Returns true if the modification was applied.
  bool modify_order(OrderId id, Tick new_price, Quantity new_qty);

  /// Look up an order by ID. Returns nullptr if not found.
  [[nodiscard]] Order *get_order(OrderId id) const;

  /// Get the best bid/ask prices. Returns INVALID_PRICE if side is empty.
  [[nodiscard]] Tick best_bid() const noexcept { return best_bid_; }
  [[nodiscard]] Tick best_ask() const noexcept { return best_ask_; }

  /// Get the front-of-queue order at the best bid/ask.
  [[nodiscard]] Order *best_bid_order() const;
  [[nodiscard]] Order *best_ask_order() const;

  /// Get the PriceLevel at a given tick (or nullptr if none).
  [[nodiscard]] PriceLevel *get_level(Side side, Tick price);

  /// Get depth snapshot: top N price levels sorted by price.
  [[nodiscard]] std::vector<DepthLevel> get_depth(Side side, int levels) const;

  /// Remove an order from its price level (internal helper, also used by
  /// MatchingEngine).
  void remove_order_from_level(Order *order);

  /// Return an order to the pool.
  void release_order(Order *order);

  /// Replenish an iceberg order's display quantity from its hidden reserve.
  /// The order is re-added to the BACK of the queue (loses time priority).
  /// Returns true if replenishment occurred.
  bool replenish_iceberg(Order *order);

  // ── Statistics ──────────────────────────────────────────────────
  [[nodiscard]] std::size_t total_orders() const noexcept { return order_map_.size(); }
  [[nodiscard]] std::size_t bid_levels() const noexcept { return bids_.size(); }
  [[nodiscard]] std::size_t ask_levels() const noexcept { return asks_.size(); }
  [[nodiscard]] std::size_t pool_available() const noexcept { return pool_.available(); }

private:
  /// Recalculate best_bid_ after removing from the bid side.
  void update_best_bid();
  /// Recalculate best_ask_ after removing from the ask side.
  void update_best_ask();

  // ── Data members ────────────────────────────────────────────────
  ObjectPool<Order, MAX_ORDERS> pool_;

  std::unordered_map<Tick, PriceLevel> bids_; // Tick → PriceLevel (buy side)
  std::unordered_map<Tick, PriceLevel> asks_; // Tick → PriceLevel (sell side)

  // Fast lookup: OrderId → Order*
  std::unordered_map<OrderId, Order *> order_map_;

  Tick best_bid_ = INVALID_PRICE;
  Tick best_ask_ = INVALID_PRICE;
};

} // namespace arena
