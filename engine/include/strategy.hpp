#pragma once
/// @file strategy.hpp
/// @brief Clean abstract interface for pluggable trading strategies.
///
/// The Strategy API allows users to implement custom market-making strategies
/// in C++ that can be benchmarked against each other in the same engine.
///
/// Usage:
///   1. Inherit from Strategy
///   2. Implement generate_quote() — returns bid/ask prices and quantities
///   3. Implement on_fill() — handle fill notifications for PnL tracking
///   4. Optionally implement on_book_update() — react to book changes
///
/// Example (Naive Symmetric MM):
/// @code
///   class NaiveMM : public Strategy {
///   public:
///     Quote generate_quote(const MarketState& s) override {
///       Tick half_spread = 5;
///       return {s.midprice - half_spread, s.midprice + half_spread, 100, 100};
///     }
///     void on_fill(const FillMsg& fill) override { /* track PnL */ }
///   };
/// @endcode
///
/// See also: AvellanedaStoikovStrategy for the canonical optimal quoting model.

#include "message.hpp"
#include "types.hpp"

namespace arena {

/// Snapshot of current market state, provided to strategies each tick.
struct MarketState {
  Tick best_bid = INVALID_PRICE;
  Tick best_ask = INVALID_PRICE;
  Tick midprice = INVALID_PRICE;
  Quantity bid_depth = 0;  // Total visible bid quantity
  Quantity ask_depth = 0;  // Total visible ask quantity
  double sigma_sq = 0.0;   // Estimated midprice variance
  int64_t inventory = 0;   // Current signed inventory (positive = long)
  int64_t step = 0;        // Simulation step (for time-horizon calculation)
  int64_t total_steps = 0; // Total simulation steps (T)
};

/// A two-sided quote (bid + ask) to submit to the book.
struct Quote {
  Tick bid_price = INVALID_PRICE;
  Tick ask_price = INVALID_PRICE;
  Quantity bid_qty = 0;
  Quantity ask_qty = 0;

  /// Whether this quote has a valid bid.
  [[nodiscard]] constexpr bool has_bid() const noexcept {
    return bid_price != INVALID_PRICE && bid_qty > 0;
  }
  /// Whether this quote has a valid ask.
  [[nodiscard]] constexpr bool has_ask() const noexcept {
    return ask_price != INVALID_PRICE && ask_qty > 0;
  }
};

/// Abstract base class for trading strategies.
/// Subclass this to implement custom market-making or directional strategies.
class Strategy {
public:
  virtual ~Strategy() = default;

  /// Generate a two-sided quote given the current market state.
  /// Called each simulation step. Return a Quote with INVALID_PRICE
  /// for bid/ask to skip quoting on that side.
  virtual Quote generate_quote(const MarketState& state) = 0;

  /// Handle a fill notification for one of our orders.
  /// Use this for inventory and PnL tracking.
  virtual void on_fill(const FillMsg& fill) = 0;

  /// React to a book update (optional override).
  /// Called when the book state changes due to any event.
  virtual void on_book_update(const BookUpdateMsg& /*update*/) {}

  /// Called at the start of the simulation with config parameters (optional).
  virtual void on_init(int64_t /*total_steps*/) {}

  /// Return a human-readable name for this strategy.
  [[nodiscard]] virtual const char *name() const { return "Strategy"; }
};

} // namespace arena
