# Agent Types

## Overview

The simulation runs a multi-agent environment where each agent type represents a distinct trading strategy. The adversarial interaction between agents creates realistic microstructure dynamics.

```
Market Maker ←→ Informed Trader    (adverse selection)
Market Maker ←→ Noise Trader       (spread capture)
Market Maker ←→ Latency Arb        (stale quote pickup)
Market Maker ←→ Momentum Trader    (autocorrelated flow)
```

## Market Maker (Avellaneda-Stoikov)

**File**: `simulation/agents/market_maker.py`

The canonical optimal market-making model from the paper *"High-frequency trading in a limit order book"* (Avellaneda & Stoikov, 2008).

### Strategy

1. **Reservation price** (inventory-adjusted fair value):
   ```
   r(s, q, t) = s − q·γ·σ²·(T−t)
   ```
   Where: `s` = midprice, `q` = inventory, `γ` = risk aversion, `σ²` = variance, `T−t` = time remaining.

2. **Optimal spread**:
   ```
   δ(t) = γ·σ²·(T−t) + (2/γ)·ln(1 + γ/κ)
   ```
   Where: `κ` = order fill intensity.

3. **Multi-level quoting**: Places quotes at `reservation ± δ/2`, `reservation ± δ/2 + spacing`, etc.

### Parameters
| Parameter | Default | Description |
|-----------|---------|-------------|
| γ (gamma) | 0.1 | Risk aversion (higher → wider spread, faster inventory reduction) |
| κ (kappa) | 1.5 | Order fill intensity (higher → tighter spread) |
| levels | 3 | Number of quote levels per side |
| level_spacing | 5 | Ticks between quote levels |
| base_qty | 100 | Quantity per quote |
| inventory_limit | 500 | Max \|inventory\| before stopping quotes |

## Noise Trader

**File**: `simulation/agents/noise_trader.py`

Random liquidity provider creating background order flow.

- With probability `order_prob`, places a random limit order within `spread_range` ticks of mid
- With small probability, sends a market order instead
- Provides the baseline liquidity that makes market making viable

## Informed Trader (Toxic Flow)

**File**: `simulation/agents/informed_trader.py`

Has access to the true price signal. Creates **adverse selection** — the primary risk for market makers.

- When `|true_price − mid| > threshold`: sends aggressive market orders
- Higher `aggression` = more frequent exploitation of edge
- This is the KEY adversarial agent in the A-S framework

## Momentum Trader

**File**: `simulation/agents/momentum_trader.py`

EMA crossover strategy creating **autocorrelated order flow**.

- Computes fast and slow EMAs of the midprice
- When `fast_ema > slow_ema + 1`: bullish → buy
- When `fast_ema < slow_ema − 1`: bearish → sell
- Creates trend-following pressure that the MM must handle

## Latency Arbitrageur

**File**: `simulation/agents/latency_arb.py`

Exploits stale quotes left by slower market makers. Only profitable with latency advantage.

- Latency: ~50µs (vs MM's ~200µs)
- When `true_price − best_ask > threshold`: buy (pick off stale ask)
- When `best_bid − true_price > threshold`: sell (pick off stale bid)
- Actively flattens inventory (not a directional trader)
- Demonstrates why latency matters for market making economics
