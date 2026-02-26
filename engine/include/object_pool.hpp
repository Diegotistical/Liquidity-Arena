#pragma once
/// @file object_pool.hpp
/// @brief Lock-free (single-threaded) object pool with O(1) alloc/dealloc.
///
/// Design rationale:
///   - Pre-allocates a contiguous block of N objects on the HEAP at
///   construction.
///   - allocate() pops from a singly-linked free list — O(1), zero heap alloc.
///   - deallocate() pushes back onto the free list — O(1).
///   - All objects reside in contiguous memory for cache friendliness.
///   - This is the ONLY way orders are created on the hot path.
///
/// Note: The backing storage uses std::vector (single heap allocation at
/// construction) rather than std::array, to avoid stack overflow for large pool
/// sizes.
///
/// Complexity:
///   - Construction: O(N) — one heap alloc + free list setup
///   - allocate():   O(1) — zero alloc
///   - deallocate(): O(1) — zero alloc
///   - Memory:       O(N) contiguous heap block

#include <cassert>
#include <cstddef>
#include <new>
#include <type_traits>
#include <vector>

namespace arena {

/// @tparam T        The type to pool. Must be default-constructible.
/// @tparam Capacity Maximum number of objects in the pool (compile-time).
template <typename T, std::size_t Capacity> class ObjectPool {
  static_assert(std::is_default_constructible_v<T>,
                "ObjectPool requires a default-constructible type");

public:
  ObjectPool() : storage_(Capacity) {
    // Build the free list: each node's "next" points to the subsequent slot.
    for (std::size_t i = 0; i < Capacity; ++i) {
      storage_[i].next_free = (i + 1 < Capacity) ? &storage_[i + 1] : nullptr;
    }
    head_ = &storage_[0];
  }

  // Non-copyable, non-movable (pointers into storage would dangle).
  ObjectPool(const ObjectPool &) = delete;
  ObjectPool &operator=(const ObjectPool &) = delete;
  ObjectPool(ObjectPool &&) = delete;
  ObjectPool &operator=(ObjectPool &&) = delete;

  /// Allocate one object from the pool. Returns nullptr if exhausted.
  /// O(1) — pops from the free list head. ZERO heap allocation.
  [[nodiscard]] T *allocate() noexcept {
    if (head_ == nullptr) [[unlikely]] {
      return nullptr; // Pool exhausted
    }
    FreeNode *node = head_;
    head_ = node->next_free;
    ++allocated_count_;

    // Placement-new a fresh T into the storage.
    T *obj = reinterpret_cast<T *>(&node->storage);
    new (obj) T{};
    return obj;
  }

  /// Return an object to the pool. O(1) — pushes onto free list head.
  /// The caller must ensure `obj` was allocated from THIS pool.
  void deallocate(T *obj) noexcept {
    assert(obj != nullptr);
    assert(owns(obj) && "Deallocating object not from this pool");

    // Call destructor, then reclaim the slot.
    obj->~T();
    FreeNode *node = reinterpret_cast<FreeNode *>(obj);
    node->next_free = head_;
    head_ = node;
    --allocated_count_;
  }

  /// Check if a pointer belongs to this pool's storage range.
  [[nodiscard]] bool owns(const T *obj) const noexcept {
    const auto *raw = reinterpret_cast<const FreeNode *>(obj);
    return raw >= storage_.data() && raw < storage_.data() + Capacity;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return Capacity; }
  [[nodiscard]] std::size_t allocated() const noexcept {
    return allocated_count_;
  }
  [[nodiscard]] std::size_t available() const noexcept {
    return Capacity - allocated_count_;
  }
  [[nodiscard]] bool full() const noexcept {
    return allocated_count_ == Capacity;
  }

private:
  /// Each slot in the pool is a union: either a live T object, or a free-list
  /// node. The free-list node only stores a "next" pointer.
  union FreeNode {
    std::aligned_storage_t<sizeof(T), alignof(T)> storage;
    FreeNode *next_free;
  };

  // Contiguous pre-allocated storage (heap-allocated via vector).
  // Single allocation at construction, then zero alloc on hot path.
  std::vector<FreeNode> storage_;

  // Head of the singly-linked free list.
  FreeNode *head_ = nullptr;

  // Tracking for diagnostics.
  std::size_t allocated_count_ = 0;
};

} // namespace arena
