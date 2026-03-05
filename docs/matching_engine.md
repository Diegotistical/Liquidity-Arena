# Matching Engine Specification

## Overview

The Liquidity Arena matching engine is a price-time priority FIFO matching engine implemented in C++20. It processes limit orders, market orders, and iceberg orders with maker/taker fee economics.

## Order Types

### Limit Order
- Rests on the book at specified price until filled, cancelled, or modified
- Non-crossing limit orders sit passively; crossing orders match immediately
- Partial fills: remainder rests on the book

### Market Order
- Matches immediately against the opposite side ("walk the book")
- No price specified (uses best available price)
- Unfilled remainder is **killed** (does NOT rest)

### Iceberg Order
- Only `display_qty` is visible to the market; `hidden_qty` is masked
- When display portion is fully consumed, a fresh slice is auto-replenished from hidden
- Replenishment sends the order to the **back of the queue** (loses time priority)
- Treated as `LIMIT` for price compatibility checks

## Matching Rules

### Price-Time Priority (FIFO)
1. **Price priority**: Best price on the opposite side matches first
   - Buy orders match against the lowest ask
   - Sell orders match against the highest bid
2. **Time priority**: At the same price, the earliest order matches first (FIFO)
3. **Maker price**: Fills execute at the **maker's price** (passive order gets price improvement)

### Crossing Behavior
When a new limit order's price crosses the spread:
```
New BID at 101, resting ASK at 100 → Fill at 100 (maker's price)
New ASK at 99, resting BID at 100 → Fill at 100 (maker's price)
```

### Book Walking
Market orders (and aggressive limit orders) walk multiple price levels:
```
ASK side: [100 × 50], [101 × 50]
Market BUY qty=100 → Fill 50@100, Fill 50@101 (two separate fills)
```

## Order Lifecycle

```
New Order → Validate → Match (if crossing) → Rest (if remainder)
                                ↓
                          Fill Event(s)
                          
Resting Order → Cancel → Remove from level + pool
              → Modify → In-place (qty down) OR cancel+re-add (price change)
```

## Queue Position Semantics

| Action | Queue Position |
|--------|---------------|
| New limit order | Back of queue at price level |
| Quantity down modify | **Preserved** (stays in place) |
| Price change modify | **Lost** — cancel + re-add to back |
| Iceberg replenish | **Lost** — back of queue |

## Fee Model

The engine applies maker/taker fees per fill:
- **Maker** (passive, resting order): receives rebate (negative fee)
- **Taker** (aggressive, incoming order): pays fee (positive fee)

```
Fee = price × quantity × rate_bps / 10000
```

Default: 2 bps maker rebate, 3 bps taker fee (matching typical US equity exchanges).

## Memory Model

### Object Pool
- Pre-allocated contiguous array of `Order` objects (`MAX_ORDERS = 1,000,000`)
- O(1) `allocate()` and `deallocate()` via free list
- **Zero heap allocation** on the matching hot path

### Intrusive Linked List
- `PriceLevel` uses intrusive doubly-linked list (prev/next pointers on `Order` itself)
- O(1) push_back (add), pop_front (fill), remove (cancel)
- No separate `std::list` node allocations

### Price Level Map
- `std::unordered_map<Tick, PriceLevel>` — O(1) amortized lookup
- Best bid/ask tracked incrementally for O(1) BBO queries

## Message Protocol

Binary TLV framing over TCP:
```
[1 byte MsgType] [4 bytes payload length (LE)] [payload bytes]
```

| Message | Direction | Description |
|---------|-----------|-------------|
| `NEW_ORDER` | Client → Engine | Submit limit/market/iceberg order |
| `CANCEL_ORDER` | Client → Engine | Cancel resting order by ID |
| `MODIFY_ORDER` | Client → Engine | Modify price and/or quantity |
| `FILL` | Engine → Client | Fill notification with fees |
| `BOOK_UPDATE` | Engine → Client | Top-10 LOB snapshot |
| `REJECT` | Engine → Client | Order rejection with reason code |

## Rejection Reasons

| Code | Reason | Description |
|------|--------|-------------|
| 0 | `INVALID_PARAMS` | Bad price, qty, or side |
| 1 | `POOL_EXHAUSTED` | Order pool is full |
| 2 | `ORDER_NOT_FOUND` | Cancel/modify target doesn't exist |
| 3 | `DUPLICATE_ID` | Order ID already in use |
| 4 | `INVALID_MODIFY` | Modify would create invalid state |
