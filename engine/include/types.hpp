#pragma once
/// @file types.hpp
/// @brief Core type aliases, enums, and constants for the matching engine.
///
/// All prices are in integer ticks — NEVER floating-point on the hot path.
/// This eliminates IEEE 754 precision errors in price comparisons and
/// matches the internal representation used by real exchanges (CME, Nasdaq).

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstdint>
#include <limits>

namespace arena {

// ── Type Aliases ─────────────────────────────────────────────────────
using OrderId = uint64_t;
using Tick = int64_t; // Price in integer ticks (e.g., $150.25 → 15025)
using Quantity = int32_t;
using Timestamp = uint64_t; // Nanoseconds since epoch

// ── Side ─────────────────────────────────────────────────────────────
enum class Side : uint8_t { BID = 0, ASK = 1 };

// ── Order Types ──────────────────────────────────────────────────────
enum class OrderType : uint8_t {
  LIMIT = 0,
  MARKET = 1,
  ICEBERG = 2 // Hidden liquidity: display_qty visible, total_qty hidden
};

// ── Reject Reasons ───────────────────────────────────────────────────
/// Reason codes for order rejection (sent in RejectMsg).
enum class RejectReason : uint8_t {
  INVALID_PARAMS = 0,  // Bad price, qty, or side
  POOL_EXHAUSTED = 1,  // Order pool is full
  ORDER_NOT_FOUND = 2, // Cancel/modify target doesn't exist
  DUPLICATE_ID = 3,    // Order ID already in use
  INVALID_MODIFY = 4   // Modify would create invalid state
};

// ── Constants ────────────────────────────────────────────────────────
constexpr Tick TICK_SIZE = 1; // 1 tick = 1 cent
constexpr Tick INVALID_PRICE = (std::numeric_limits<Tick>::min)();
constexpr OrderId INVALID_ORDER = 0;

// ── Fee Schedule ─────────────────────────────────────────────────────
/// Maker/taker fee model. Fees are in basis points (1 bp = 0.01%).
/// maker_rebate is EARNED by the passive side; taker_fee is PAID by aggressor.
struct FeeSchedule {
  double maker_rebate_bps = 2.0; // Maker earns 0.02% per fill
  double taker_fee_bps = 3.0;    // Taker pays 0.03% per fill

  /// Compute maker rebate in ticks for a given fill.
  [[nodiscard]] constexpr double maker_rebate(Tick price, Quantity qty) const noexcept {
    return static_cast<double>(price) * static_cast<double>(qty) * maker_rebate_bps / 10000.0;
  }

  /// Compute taker fee in ticks for a given fill.
  [[nodiscard]] constexpr double taker_fee(Tick price, Quantity qty) const noexcept {
    return static_cast<double>(price) * static_cast<double>(qty) * taker_fee_bps / 10000.0;
  }
};

// ── Conversion Utilities ─────────────────────────────────────────────

/// Convert a floating-point dollar price to integer ticks.
/// Only used at the boundary (input parsing). Never on the hot path.
constexpr Tick price_to_ticks(double price, double tick_size = 0.01) {
  return static_cast<Tick>(price / tick_size + 0.5);
}

/// Convert integer ticks back to dollar price (for display only).
constexpr double ticks_to_price(Tick ticks, double tick_size = 0.01) {
  return static_cast<double>(ticks) * tick_size;
}

} // namespace arena
