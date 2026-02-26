/// @file matching_engine.cpp
/// @brief MatchingEngine implementation — the hot path.
///
/// Performance-critical code: no heap allocation, no exceptions,
/// no virtual dispatch in the matching loop.

#include "matching_engine.hpp"

#include <algorithm>
#include <cstring>


namespace arena {

Order *MatchingEngine::process_new_order(OrderId id, Side side, OrderType type, Tick price,
                                         Quantity quantity, Timestamp ts) {
  ++total_orders_;

  // Input validation.
  if (quantity <= 0) [[unlikely]] {
    return nullptr;
  }
  if (type == OrderType::LIMIT && price <= 0) [[unlikely]] {
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

  // Limit order: add to book first, then attempt to match if crossing.
  Order *order = book_.add_order(id, side, price, quantity, ts);
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

Quantity MatchingEngine::match_order(Order *incoming) {
  // Determine which side of the book to match against.
  const bool is_buy = (incoming->side == Side::BID);

  while (incoming->remaining() > 0) {
    // Find the best price on the opposite side.
    Tick opposite_best = is_buy ? book_.best_ask() : book_.best_bid();
    if (opposite_best == INVALID_PRICE)
      break; // Empty book

    // Check price compatibility.
    if (incoming->type == OrderType::LIMIT) {
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
    Quantity fill_qty = std::min(incoming->remaining(), maker->remaining());
    execute_fill(maker, incoming, fill_qty);

    // If maker is fully filled, remove from book.
    if (maker->is_filled()) {
      book_.remove_order_from_level(maker);
      book_.release_order(maker);
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

  // Emit fill event.
  if (on_fill_) {
    FillMsg fill;
    fill.maker_id = maker->id;
    fill.taker_id = taker->id;
    fill.price = fill_price;
    fill.quantity = fill_qty;
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
