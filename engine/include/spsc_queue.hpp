#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace arena {

/// @brief A lock-free Single-Producer Single-Consumer queue based on a circular buffer.
/// Used to pass messages between the I/O thread and the Matching Engine thread
/// without locking.
template <typename T>
class SPSCQueue {
public:
  explicit SPSCQueue(std::size_t capacity) : capacity_(capacity), buffer_(capacity + 1) {}

  /// Disallow copy and assignment
  SPSCQueue(const SPSCQueue&) = delete;
  SPSCQueue& operator=(const SPSCQueue&) = delete;

  /// Push an item. Returns true if successful, false if queue is full.
  bool push(const T& item) {
    const std::size_t write_pos = write_idx_.load(std::memory_order_relaxed);
    const std::size_t next_write_pos = (write_pos + 1) % buffer_.size();
    
    if (next_write_pos == read_idx_.load(std::memory_order_acquire)) {
      return false; // Queue full
    }
    
    buffer_[write_pos] = item;
    write_idx_.store(next_write_pos, std::memory_order_release);
    return true;
  }

  /// Pop an item. Returns true if successful, false if queue is empty.
  bool pop(T& item) {
    const std::size_t read_pos = read_idx_.load(std::memory_order_relaxed);
    
    if (read_pos == write_idx_.load(std::memory_order_acquire)) {
      return false; // Queue empty
    }
    
    item = buffer_[read_pos];
    read_idx_.store((read_pos + 1) % buffer_.size(), std::memory_order_release);
    return true;
  }

private:
  std::size_t capacity_;
  std::vector<T> buffer_;

  // Align to cache lines to prevent false sharing between producer and consumer threads
  alignas(64) std::atomic<std::size_t> write_idx_{0};
  alignas(64) std::atomic<std::size_t> read_idx_{0};
};

} // namespace arena
