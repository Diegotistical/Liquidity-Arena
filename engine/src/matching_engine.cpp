/// @file matching_engine.cpp
/// @brief MatchingEngine implementation — the hot path.
///
/// Performance-critical code: no heap allocation, no exceptions,
/// no virtual dispatch in the matching loop.
///
/// Supports: limit orders, market orders, iceberg orders, order modification,
/// and maker/taker fee calculations.

#include "matching_engine.hpp"

#include <algorithm>
#include <cstring>

namespace arena {

Order *MatchingEngine::process_new_order(OrderId id, Side side, OrderType type, Tick price,
                                         Quantity quantity, Timestamp ts, Quantity display_qty) {
  ++total_orders_;

  // Input validation.
  if (quantity <= 0) [[unlikely]] {
    return nullptr;
  }
  if (type == OrderType::LIMIT && price <= 0) [[unlikely]] {
    return nullptr;
  }
  if (type == OrderType::ICEBERG && (price <= 0 || display_qty <= 0)) [[unlikely]] {
    return nullptr;
  }

  if (type == OrderType::MARKET) {
    // Market orders: create a temporary order for matching, then discard.
    // Use the worst possible price to ensure it crosses.
    Tick market_price =
        (side == Side::BID) ? std::numeric_limits<Tick>::max() : std::numeric_limits<Tick>::min();

    // Allocate temporarily from the pool.
    Order *order = book_.add_order(id, side, market_price, quantity, ts);
    if (order == nullptr)
      return nullptr;

    order->type = OrderType::MARKET;
    Quantity remaining = match_order(order);

    // Market orders don't rest — remove any unfilled remainder.
    if (remaining > 0) {
      book_.remove_order_from_level(order);
      book_.release_order(order);
    }
    return nullptr; // Market orders never rest
  }

  // Iceberg order: add with display/hidden split.
  Order *order = nullptr;
  if (type == OrderType::ICEBERG) {
    order = book_.add_iceberg_order(id, side, price, quantity, display_qty, ts);
  } else {
    // Regular limit order.
    order = book_.add_order(id, side, price, quantity, ts);
  }

  if (order == nullptr)
    return nullptr;

  // Check if this limit order crosses the spread (is aggressive).
  bool crosses = false;
  if (side == Side::BID && book_.best_ask() != INVALID_PRICE) {
    crosses = (price >= book_.best_ask());
  } else if (side == Side::ASK && book_.best_bid() != INVALID_PRICE) {
    crosses = (price <= book_.best_bid());
  }

  if (crosses) {
    Quantity remaining = match_order(order);
    if (remaining <= 0) {
      // Fully filled — remove from book and release.
      book_.remove_order_from_level(order);
      book_.release_order(order);
      return nullptr;
    }
    // Partially filled — order rests with reduced quantity.
  }

  return order; // Resting order
}

bool MatchingEngine::process_cancel(OrderId id) {
  bool success = book_.cancel_order(id);
  if (success) {
    ++total_cancels_;
  }
  return success;
}

bool MatchingEngine::process_modify(OrderId id, Tick new_price, Quantity new_qty) {
  bool success = book_.modify_order(id, new_price, new_qty);
  if (success) {
    ++total_modifies_;
  }
  return success;
}

Quantity MatchingEngine::match_order(Order *incoming) {
  // Determine which side of the book to match against.
  const bool is_buy = (incoming->side == Side::BID);

  while (incoming->remaining() > 0) {
    // Find the best price on the opposite side.
    Tick opposite_best = is_buy ? book_.best_ask() : book_.best_bid();
    if (opposite_best == INVALID_PRICE)
      break; // Empty book

    // Check price compatibility.
    if (incoming->type == OrderType::LIMIT || incoming->type == OrderType::ICEBERG) {
      if (is_buy && incoming->price < opposite_best)
        break; // Bid below best ask
      if (!is_buy && incoming->price > opposite_best)
        break; // Ask above best bid
    }

    // Get the front-of-queue order at the best price (time priority).
    Order *maker = is_buy ? book_.best_ask_order() : book_.best_bid_order();
    if (maker == nullptr)
      break;

    // Determine fill quantity.
    // For iceberg makers, only the visible portion can be filled at this step.
    Quantity maker_available = maker->remaining();
    if (maker->type == OrderType::ICEBERG && maker->display_qty > 0) {
      // Only the display portion is available for this match cycle.
      maker_available = std::min(maker_available, static_cast<Quantity>(maker->display_qty));
    }

    Quantity fill_qty = std::min(incoming->remaining(), maker_available);
    execute_fill(maker, incoming, fill_qty);

    // If maker is fully filled (or display portion consumed).
    if (maker->is_filled()) {
      book_.remove_order_from_level(maker);
      book_.release_order(maker);
    } else if (maker->type == OrderType::ICEBERG) {
      // Check if display portion is consumed but hidden remains.
      Quantity display_remaining =
          maker->display_qty - (maker->filled_quantity % maker->display_qty);
      if (display_remaining <= 0 && maker->has_hidden_qty()) {
        // Replenish: move to back of queue with fresh display slice.
        book_.replenish_iceberg(maker);
      }
    }
  }

  return incoming->remaining();
}

void MatchingEngine::execute_fill(Order *maker, Order *taker, Quantity fill_qty) {
  // Fill at the MAKER's price (price improvement for aggressive orders).
  Tick fill_price = maker->price;

  maker->filled_quantity += fill_qty;
  taker->filled_quantity += fill_qty;

  // Update the PriceLevel's total quantity.
  PriceLevel *level = book_.get_level(maker->side, maker->price);
  if (level != nullptr) {
    level->reduce_quantity(fill_qty);
  }

  // Also update taker's level quantity.
  PriceLevel *taker_level = book_.get_level(taker->side, taker->price);
  if (taker_level != nullptr) {
    taker_level->reduce_quantity(fill_qty);
  }

  ++total_fills_;

  // Compute fees.
  double maker_fee = -fees_.maker_rebate(fill_price, fill_qty); // Negative = earned
  double taker_fee_val = fees_.taker_fee(fill_price, fill_qty); // Positive = paid
  total_maker_rebates_ += (-maker_fee);
  total_taker_fees_ += taker_fee_val;

  // Emit fill event.
  if (on_fill_) {
    FillMsg fill;
    fill.maker_id = maker->id;
    fill.taker_id = taker->id;
    fill.price = fill_price;
    fill.quantity = fill_qty;
    fill.maker_fee = maker_fee;
    fill.taker_fee = taker_fee_val;
    on_fill_(fill);
  }
}

BookUpdateMsg MatchingEngine::get_book_snapshot() const {
  BookUpdateMsg msg;
  std::memset(&msg, 0, sizeof(msg));

  msg.best_bid = book_.best_bid();
  msg.best_ask = book_.best_ask();

  // Get bid depth (sorted descending).
  auto bid_depth = book_.get_depth(Side::BID, MAX_DEPTH_LEVELS);
  msg.num_bid_levels = static_cast<int32_t>(bid_depth.size());
  for (int i = 0; i < msg.num_bid_levels; ++i) {
    msg.bid_prices[i] = bid_depth[static_cast<std::size_t>(i)].price;
    msg.bid_quantities[i] = bid_depth[static_cast<std::size_t>(i)].quantity;
  }

  // Get ask depth (sorted ascending).
  auto ask_depth = book_.get_depth(Side::ASK, MAX_DEPTH_LEVELS);
  msg.num_ask_levels = static_cast<int32_t>(ask_depth.size());
  for (int i = 0; i < msg.num_ask_levels; ++i) {
    msg.ask_prices[i] = ask_depth[static_cast<std::size_t>(i)].price;
    msg.ask_quantities[i] = ask_depth[static_cast<std::size_t>(i)].quantity;
  }

  return msg;
}

} // namespace arena
