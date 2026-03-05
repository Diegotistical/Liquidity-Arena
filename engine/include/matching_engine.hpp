#pragma once
/// @file matching_engine.hpp
/// @brief Price-time priority FIFO matching engine.
///
/// Handles limit orders, market orders, iceberg orders, modifications,
/// and cancellations. Emits Fill messages for each match. No heap allocation
/// on the hot path (all orders come from the ObjectPool via OrderBook).
///
/// Matching rules (identical to real exchanges):
///   - Limit buy crossing the ask: match against resting asks at their price
///   (maker gets price improvement)
///   - Limit sell crossing the bid: match against resting bids at their price
///   - Market orders: walk the book until filled or book exhausted
///   - Iceberg orders: display portion matches first; hidden replenishes
///   automatically when display is consumed (loses time priority on replenish)
///   - Partial fills: remainder rests (limit) or is killed (market)
///
/// Fee model:
///   - Maker receives a rebate per fill (incentivizes passive liquidity)
///   - Taker pays a fee per fill (cost of aggression)
///   - Applied per-fill in execute_fill()

#include "message.hpp"
#include "order_book.hpp"

#include <functional>
#include <vector>

namespace arena {

/// Callback type for fill events.
using FillCallback = std::function<void(const FillMsg&)>;

/// The matching engine processes incoming orders against the LOB.
class MatchingEngine {
public:
  explicit MatchingEngine(OrderBook& book) : book_(book) {}

  /// Set a callback to receive fill notifications.
  void set_fill_callback(FillCallback cb) { on_fill_ = std::move(cb); }

  /// Set the fee schedule for maker/taker economics.
  void set_fee_schedule(const FeeSchedule& fees) { fees_ = fees; }

  /// Get the current fee schedule.
  [[nodiscard]] const FeeSchedule& fee_schedule() const noexcept { return fees_; }

  /// Process a new order (limit, market, or iceberg).
  /// Returns the resting order pointer if any quantity remains and it's a limit
  /// order. Returns nullptr if fully filled or if it's a market order (killed
  /// if unfilled).
  Order *process_new_order(OrderId id, Side side, OrderType type, Tick price, Quantity quantity,
                           Timestamp ts, Quantity display_qty = 0);

  /// Cancel an existing order by ID. Returns true if successfully cancelled.
  bool process_cancel(OrderId id);

  /// Modify an existing order's price and/or quantity.
  /// - Price change: loses queue position (cancel + re-add).
  /// - Quantity down: keeps queue position (in-place).
  /// - Quantity up: rejected (not allowed — would break fairness).
  /// Returns true if modification was applied.
  bool process_modify(OrderId id, Tick new_price, Quantity new_qty);

  /// Get a snapshot of the book for broadcasting.
  BookUpdateMsg get_book_snapshot() const;

  // ── Statistics ──────────────────────────────────────────────────
  [[nodiscard]] uint64_t total_fills() const noexcept { return total_fills_; }
  [[nodiscard]] uint64_t total_orders() const noexcept { return total_orders_; }
  [[nodiscard]] uint64_t total_cancels() const noexcept { return total_cancels_; }
  [[nodiscard]] uint64_t total_modifies() const noexcept { return total_modifies_; }
  [[nodiscard]] double total_maker_rebates() const noexcept { return total_maker_rebates_; }
  [[nodiscard]] double total_taker_fees() const noexcept { return total_taker_fees_; }

private:
  /// Match an incoming order against the opposite side of the book.
  /// Returns the remaining quantity after matching.
  Quantity match_order(Order *incoming);

  /// Execute a single fill between maker and taker.
  void execute_fill(Order *maker, Order *taker, Quantity fill_qty);

  OrderBook& book_;
  FillCallback on_fill_;
  FeeSchedule fees_;

  uint64_t total_fills_ = 0;
  uint64_t total_orders_ = 0;
  uint64_t total_cancels_ = 0;
  uint64_t total_modifies_ = 0;
  double total_maker_rebates_ = 0.0;
  double total_taker_fees_ = 0.0;
};

} // namespace arena
