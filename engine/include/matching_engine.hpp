#pragma once
/// @file matching_engine.hpp
/// @brief Price-time priority FIFO matching engine.
///
/// Handles limit orders, market orders, and cancellations.
/// Emits Fill messages for each match. No heap allocation on the hot path
/// (all orders come from the ObjectPool via OrderBook).
///
/// Matching rules (identical to real exchanges):
///   - Limit buy crossing the ask: match against resting asks at their price
///   (maker gets price improvement)
///   - Limit sell crossing the bid: match against resting bids at their price
///   - Market orders: walk the book until filled or book exhausted
///   - Partial fills: remainder rests (limit) or is killed (market)

#include "message.hpp"
#include "order_book.hpp"
#include <functional>
#include <vector>


namespace arena {

/// Callback type for fill events.
using FillCallback = std::function<void(const FillMsg &)>;

/// The matching engine processes incoming orders against the LOB.
class MatchingEngine {
public:
  explicit MatchingEngine(OrderBook &book) : book_(book) {}

  /// Set a callback to receive fill notifications.
  void set_fill_callback(FillCallback cb) { on_fill_ = std::move(cb); }

  /// Process a new order (limit or market).
  /// Returns the resting order pointer if any quantity remains and it's a limit
  /// order. Returns nullptr if fully filled or if it's a market order (killed
  /// if unfilled).
  Order *process_new_order(OrderId id, Side side, OrderType type, Tick price,
                           Quantity quantity, Timestamp ts);

  /// Cancel an existing order by ID. Returns true if successfully cancelled.
  bool process_cancel(OrderId id);

  /// Get a snapshot of the book for broadcasting.
  BookUpdateMsg get_book_snapshot() const;

  // ── Statistics ──────────────────────────────────────────────────
  [[nodiscard]] uint64_t total_fills() const noexcept { return total_fills_; }
  [[nodiscard]] uint64_t total_orders() const noexcept { return total_orders_; }
  [[nodiscard]] uint64_t total_cancels() const noexcept {
    return total_cancels_;
  }

private:
  /// Match an incoming order against the opposite side of the book.
  /// Returns the remaining quantity after matching.
  Quantity match_order(Order *incoming);

  /// Execute a single fill between maker and taker.
  void execute_fill(Order *maker, Order *taker, Quantity fill_qty);

  OrderBook &book_;
  FillCallback on_fill_;

  uint64_t total_fills_ = 0;
  uint64_t total_orders_ = 0;
  uint64_t total_cancels_ = 0;
};

} // namespace arena
