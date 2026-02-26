/// @file order_book.cpp
/// @brief OrderBook implementation.

#include "order_book.hpp"

namespace arena {

Order *OrderBook::add_order(OrderId id, Side side, Tick price,
                            Quantity quantity, Timestamp ts) {
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

  // Insert into the appropriate side's price level.
  auto &levels = (side == Side::BID) ? bids_ : asks_;
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
  auto &levels = (side == Side::BID) ? bids_ : asks_;
  auto it = levels.find(price);
  return (it != levels.end()) ? &it->second : nullptr;
}

std::vector<DepthLevel> OrderBook::get_depth(Side side, int levels) const {
  const auto &level_map = (side == Side::BID) ? bids_ : asks_;
  std::vector<DepthLevel> result;
  result.reserve(static_cast<std::size_t>(levels));

  // Collect all non-empty price levels.
  for (const auto &[price, level] : level_map) {
    if (!level.empty()) {
      result.push_back({price, level.total_quantity()});
    }
  }

  // Sort: bids descending (best bid first), asks ascending (best ask first).
  if (side == Side::BID) {
    std::sort(result.begin(), result.end(),
              [](const DepthLevel &a, const DepthLevel &b) {
                return a.price > b.price;
              });
  } else {
    std::sort(result.begin(), result.end(),
              [](const DepthLevel &a, const DepthLevel &b) {
                return a.price < b.price;
              });
  }

  // Truncate to requested depth.
  if (static_cast<int>(result.size()) > levels) {
    result.resize(static_cast<std::size_t>(levels));
  }

  return result;
}

void OrderBook::remove_order_from_level(Order *order) {
  auto &levels = (order->side == Side::BID) ? bids_ : asks_;
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

void OrderBook::update_best_bid() {
  if (bids_.empty()) {
    best_bid_ = INVALID_PRICE;
    return;
  }
  best_bid_ = INVALID_PRICE;
  for (const auto &[price, level] : bids_) {
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
  for (const auto &[price, level] : asks_) {
    if (!level.empty() && (best_ask_ == INVALID_PRICE || price < best_ask_)) {
      best_ask_ = price;
    }
  }
}

} // namespace arena
