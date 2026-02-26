#pragma once
/// @file types.hpp
/// @brief Core type aliases and constants for the matching engine.
/// All prices are in integer ticks — NEVER floating-point on the hot path.
/// This eliminates IEEE 754 precision errors in price comparisons.

#include <cstdint>
#include <limits>

namespace arena {

// ── Type Aliases ─────────────────────────────────────────────────────
using OrderId   = uint64_t;
using Tick      = int64_t;     // Price in integer ticks (e.g., $150.25 → 15025)
using Quantity  = int32_t;
using Timestamp = uint64_t;    // Nanoseconds since epoch

// ── Enums ────────────────────────────────────────────────────────────
enum class Side : uint8_t {
    BID = 0,
    ASK = 1
};

enum class OrderType : uint8_t {
    LIMIT  = 0,
    MARKET = 1
};

// ── Constants ────────────────────────────────────────────────────────
constexpr Tick     TICK_SIZE      = 1;   // 1 tick = 1 cent
constexpr Tick     INVALID_PRICE  = std::numeric_limits<Tick>::min();
constexpr OrderId  INVALID_ORDER  = 0;

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
