#pragma once
/// @file price_level.hpp
/// @brief A single price level in the LOB — an intrusive doubly-linked list of
/// orders.
///
/// Design rationale:
///   - Intrusive list avoids separate node allocations (orders carry their own
///   prev/next).
///   - push_back: O(1) — append to tail (FIFO time priority).
///   - pop_front: O(1) — remove the oldest order (first to match).
///   - remove:    O(1) — cancel any order by pointer (given doubly-linked).
///   - total_quantity maintained incrementally — no traversal needed for depth
///   queries.
///   - queue_volume_ahead: O(n) — walks from head to compute volume ahead of
///   a given order. Used for realistic queue position tracking.

#include "order.hpp"

#include <cassert>
#include <cstddef>

namespace arena {

/// Manages all orders resting at a single tick price.
/// Orders are maintained in FIFO order (time priority) via an intrusive
/// doubly-linked list.
class PriceLevel {
public:
  PriceLevel() = default;

  /// Append an order to the back of the queue (newest = lowest time priority).
  /// O(1). The order's prev/next pointers are set by this method.
  void push_back(Order *order) noexcept {
    assert(order != nullptr);
    assert(order->prev == nullptr && order->next == nullptr);

    order->prev = tail_;
    order->next = nullptr;

    if (tail_ != nullptr) {
      tail_->next = order;
    } else {
      head_ = order; // First order at this level
    }
    tail_ = order;

    total_quantity_ += order->remaining();
    ++order_count_;
  }

  /// Remove and return the front order (oldest = highest time priority).
  /// O(1). Used when matching fills the front-of-queue order.
  Order *pop_front() noexcept {
    if (head_ == nullptr)
      return nullptr;

    Order *order = head_;
    head_ = order->next;

    if (head_ != nullptr) {
      head_->prev = nullptr;
    } else {
      tail_ = nullptr; // List is now empty
    }

    total_quantity_ -= order->remaining();
    --order_count_;

    order->prev = nullptr;
    order->next = nullptr;
    return order;
  }

  /// Remove a specific order from anywhere in the list (cancel operation).
  /// O(1) because we have prev/next pointers on the order itself.
  void remove(Order *order) noexcept {
    assert(order != nullptr);

    if (order->prev != nullptr) {
      order->prev->next = order->next;
    } else {
      head_ = order->next; // Was the head
    }

    if (order->next != nullptr) {
      order->next->prev = order->prev;
    } else {
      tail_ = order->prev; // Was the tail
    }

    total_quantity_ -= order->remaining();
    --order_count_;

    order->prev = nullptr;
    order->next = nullptr;
  }

  /// Update total_quantity when a partial fill occurs (without removing the
  /// order).
  void reduce_quantity(Quantity amount) noexcept {
    assert(amount > 0);
    total_quantity_ -= amount;
  }

  /// Compute total volume ahead of a given order in the queue.
  /// O(n) where n = orders ahead. Used for queue position tracking.
  /// Returns 0 for the front-of-queue order.
  [[nodiscard]] Quantity queue_volume_ahead(const Order *order) const noexcept {
    assert(order != nullptr);
    Quantity vol = 0;
    const Order *cur = head_;
    while (cur != nullptr && cur != order) {
      vol += cur->remaining();
      cur = cur->next;
    }
    return vol;
  }

  /// Modify an order's quantity in-place (for down-modifications only).
  /// Preserves queue position. The caller must ensure new_qty < current qty.
  /// Returns the quantity delta (positive = amount removed).
  Quantity modify_qty_down(Order *order, Quantity new_qty) noexcept {
    assert(order != nullptr);
    assert(new_qty > 0);
    Quantity delta = order->quantity - order->filled_quantity - new_qty;
    assert(delta > 0); // Must be a down-modification

    order->quantity = order->filled_quantity + new_qty;
    total_quantity_ -= delta;
    return delta;
  }

  // ── Accessors ───────────────────────────────────────────────────
  [[nodiscard]] Order *front() const noexcept { return head_; }
  [[nodiscard]] Quantity total_quantity() const noexcept { return total_quantity_; }
  [[nodiscard]] std::size_t order_count() const noexcept { return order_count_; }
  [[nodiscard]] bool empty() const noexcept { return head_ == nullptr; }

private:
  Order *head_ = nullptr;
  Order *tail_ = nullptr;
  Quantity total_quantity_ = 0;
  std::size_t order_count_ = 0;
};

} // namespace arena
