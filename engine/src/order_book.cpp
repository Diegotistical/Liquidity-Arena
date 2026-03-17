/// @file order_book.cpp
/// @brief OrderBook implementation.
///
/// Supports limit orders, iceberg orders, order modification,
/// and queue-position-preserving down-modifications.

#include "order_book.hpp"

namespace arena {

Order *OrderBook::add_order(OrderId id, Side side, Tick price, Quantity quantity, Timestamp ts) {
  if (order_map_.find(id) != order_map_.end()) {
    return nullptr; // Duplicate order ID
  }

  Order *order = pool_.allocate();
  if (order == nullptr) [[unlikely]] {
    return nullptr; // Pool exhausted
  }

  order->id = id;
  order->side = side;
  order->price = price;
  order->quantity = quantity;
  order->timestamp = ts;
  order->type = OrderType::LIMIT;
  order->display_qty = 0; // Fully visible
  order->hidden_qty = 0;

  // Insert into the appropriate side's price level.
  auto& levels = (side == Side::BID) ? bids_ : asks_;
  levels[price].push_back(order);

  // Track in the ID → Order* map for fast cancel lookups.
  order_map_[id] = order;

  // Update best bid/ask.
  if (side == Side::BID) {
    if (best_bid_ == INVALID_PRICE || price > best_bid_) {
      best_bid_ = price;
    }
  } else {
    if (best_ask_ == INVALID_PRICE || price < best_ask_) {
      best_ask_ = price;
    }
  }

  return order;
}

Order *OrderBook::add_iceberg_order(OrderId id, Side side, Tick price, Quantity total_qty,
                                    Quantity display_qty, Timestamp ts) {
  if (order_map_.find(id) != order_map_.end()) {
    return nullptr; // Duplicate order ID
  }

  Order *order = pool_.allocate();
  if (order == nullptr) [[unlikely]] {
    return nullptr;
  }

  order->id = id;
  order->side = side;
  order->price = price;
  order->quantity = total_qty;
  order->timestamp = ts;
  order->type = OrderType::ICEBERG;
  order->display_qty = display_qty;
  order->hidden_qty = total_qty - display_qty;

  // For the price level, only the display portion counts toward visible depth.
  auto& levels = (side == Side::BID) ? bids_ : asks_;
  // Temporarily set quantity to display portion for push_back accounting.
  Quantity saved_qty = order->quantity;
  order->quantity = display_qty;
  order->filled_quantity = 0;
  levels[price].push_back(order);
  // Restore full quantity.
  order->quantity = saved_qty;

  order_map_[id] = order;

  if (side == Side::BID) {
    if (best_bid_ == INVALID_PRICE || price > best_bid_) {
      best_bid_ = price;
    }
  } else {
    if (best_ask_ == INVALID_PRICE || price < best_ask_) {
      best_ask_ = price;
    }
  }

  return order;
}

bool OrderBook::cancel_order(OrderId id) {
  auto it = order_map_.find(id);
  if (it == order_map_.end()) {
    return false;
  }

  Order *order = it->second;
  remove_order_from_level(order);
  order_map_.erase(it);
  pool_.deallocate(order);
  return true;
}

bool OrderBook::modify_order(OrderId id, Tick new_price, Quantity new_qty) {
  auto it = order_map_.find(id);
  if (it == order_map_.end()) {
    return false;
  }

  Order *order = it->second;

  // Reject invalid modifications.
  if (new_qty <= 0) {
    return false;
  }

  // Reject quantity increases (would break queue fairness).
  if (new_qty > order->remaining()) {
    return false;
  }

  if (new_price == order->price) {
    // Same price, quantity decrease only → in-place modify (keeps queue
    // position).
    if (new_qty < order->remaining()) {
      PriceLevel *level = get_level(order->side, order->price);
      if (level != nullptr) {
        level->modify_qty_down(order, new_qty);
      }
    }
    return true;
  }

  // Price change → cancel and re-add (loses queue position).
  Side side = order->side;
  Timestamp ts = order->timestamp;
  OrderType type = order->type;
  Quantity display = order->display_qty;

  remove_order_from_level(order);
  order_map_.erase(it);
  pool_.deallocate(order);

  // Re-add at new price.
  if (type == OrderType::ICEBERG) {
    add_iceberg_order(id, side, new_price, new_qty, display, ts);
  } else {
    add_order(id, side, new_price, new_qty, ts);
  }

  return true;
}

Order *OrderBook::get_order(OrderId id) const {
  auto it = order_map_.find(id);
  return (it != order_map_.end()) ? it->second : nullptr;
}

Order *OrderBook::best_bid_order() const {
  if (best_bid_ == INVALID_PRICE)
    return nullptr;
  auto it = bids_.find(best_bid_);
  if (it == bids_.end())
    return nullptr;
  return it->second.front();
}

Order *OrderBook::best_ask_order() const {
  if (best_ask_ == INVALID_PRICE)
    return nullptr;
  auto it = asks_.find(best_ask_);
  if (it == asks_.end())
    return nullptr;
  return it->second.front();
}

PriceLevel *OrderBook::get_level(Side side, Tick price) {
  auto& levels = (side == Side::BID) ? bids_ : asks_;
  auto it = levels.find(price);
  return (it != levels.end()) ? &it->second : nullptr;
}

std::vector<DepthLevel> OrderBook::get_depth(Side side, int levels) const {
  const auto& level_map = (side == Side::BID) ? bids_ : asks_;
  std::vector<DepthLevel> result;
  result.reserve(static_cast<std::size_t>(levels));

  // Collect all non-empty price levels.
  for (const auto& [price, level] : level_map) {
    if (!level.empty()) {
      result.push_back({price, level.total_quantity()});
    }
  }

  // Sort: bids descending (best bid first), asks ascending (best ask first).
  if (side == Side::BID) {
    std::sort(result.begin(), result.end(),
              [](const DepthLevel& a, const DepthLevel& b) { return a.price > b.price; });
  } else {
    std::sort(result.begin(), result.end(),
              [](const DepthLevel& a, const DepthLevel& b) { return a.price < b.price; });
  }

  // Truncate to requested depth.
  if (static_cast<int>(result.size()) > levels) {
    result.resize(static_cast<std::size_t>(levels));
  }

  return result;
}

void OrderBook::remove_order_from_level(Order *order) {
  auto& levels = (order->side == Side::BID) ? bids_ : asks_;
  auto it = levels.find(order->price);
  if (it == levels.end())
    return;

  it->second.remove(order);

  // If the price level is now empty, remove it and update best price.
  if (it->second.empty()) {
    Tick removed_price = order->price;
    levels.erase(it);

    if (order->side == Side::BID && removed_price == best_bid_) {
      update_best_bid();
    } else if (order->side == Side::ASK && removed_price == best_ask_) {
      update_best_ask();
    }
  }
}

void OrderBook::release_order(Order *order) {
  order_map_.erase(order->id);
  pool_.deallocate(order);
}

bool OrderBook::replenish_iceberg(Order *order) {
  if (order == nullptr || !order->has_hidden_qty()) {
    return false;
  }

  // Determine how much to replenish.
  Quantity replenish = std::min(order->display_qty, order->hidden_qty);
  order->hidden_qty -= replenish;

  // Remove from current position (it goes to back of queue).
  auto& levels = (order->side == Side::BID) ? bids_ : asks_;
  auto it = levels.find(order->price);
  if (it == levels.end()) {
    return false;
  }

  it->second.remove(order);

  // Reset filled quantity for the new display slice.
  order->filled_quantity = order->quantity - order->hidden_qty - replenish;
  order->quantity = order->filled_quantity + replenish + order->hidden_qty;

  // Re-add to back of queue (loses time priority).
  // Temporarily adjust quantity for accounting.
  Quantity saved_qty = order->quantity;
  Quantity saved_filled = order->filled_quantity;
  order->quantity = replenish;
  order->filled_quantity = 0;
  it->second.push_back(order);
  order->quantity = saved_qty;
  order->filled_quantity = saved_filled;

  return true;
}

void OrderBook::update_best_bid() {
  if (bids_.empty()) {
    best_bid_ = INVALID_PRICE;
    return;
  }
  best_bid_ = INVALID_PRICE;
  for (const auto& [price, level] : bids_) {
    if (!level.empty() && (best_bid_ == INVALID_PRICE || price > best_bid_)) {
      best_bid_ = price;
    }
  }
}

void OrderBook::update_best_ask() {
  if (asks_.empty()) {
    best_ask_ = INVALID_PRICE;
    return;
  }
  best_ask_ = INVALID_PRICE;
  for (const auto& [price, level] : asks_) {
    if (!level.empty() && (best_ask_ == INVALID_PRICE || price < best_ask_)) {
      best_ask_ = price;
    }
  }
}

} // namespace arena
