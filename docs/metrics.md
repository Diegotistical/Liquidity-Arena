# Quantitative Metrics

## Core Metrics

### Sharpe Ratio
```
Sharpe = mean(returns) / std(returns) × √annualization
```
Risk-adjusted performance. Uses step-level PnL differences as returns.
- **> 2.0**: Excellent
- **1.0–2.0**: Good
- **< 1.0**: Mediocre (not worth the risk)

### Adverse Selection (THE Key Metric)
```
Adverse Selection = avg(mid_at_fill − mid_after_fill)    [for buys]
                   avg(mid_after_fill − mid_at_fill)    [for sells]
```
Positive = getting picked off by informed traders. This measures how much the market moves against you immediately after each fill.
- **Best case**: negative (market moves in your favor → you have alpha)
- **Typical MM**: slightly positive (cost of providing liquidity)
- **Worst case**: highly positive (getting destroyed by toxic flow)

### Fill Ratio
```
Fill Ratio = total_fills / total_orders_sent
```
Execution quality. Higher = more efficient.
- **> 0.5**: Orders are getting filled often
- **< 0.1**: Mostly cancelling without execution

### Inventory Variance
```
Inventory Variance = var(inventory_path)
```
How well you control inventory risk.
- **Low variance**: disciplined risk management
- **High variance**: inventory is oscillating wildly

### Quote Lifetime
```
Quote Lifetime = avg(cancel_time − place_time)
```
Average time a quote is live before cancel/fill. Shorter = faster cancellation rate.
- Real HFT: < 1 second
- This sim: measured in simulation steps

### Order-to-Trade Ratio
```
OTR = total_orders / total_fills
```
Market making efficiency. Real exchanges see 10:1 to 100:1.
- **< 5**: Very efficient (few cancels)
- **10–50**: Normal MM behavior
- **> 100**: Excessive quoting

## PnL Decomposition

The most important analysis: understanding WHERE your PnL comes from.

```
PnL = Spread Capture + Inventory PnL − Adverse Selection − Fees
```

| Component | Formula | Interpretation |
|-----------|---------|----------------|
| **Spread Capture** | Σ(\|fill_price − mid_at_fill\|) for maker fills | Revenue from providing liquidity |
| **Inventory PnL** | Σ(inventory × Δmid) | Mark-to-market from directional exposure |
| **Adverse Selection** | Σ(mid_move_against_us after fill) | Cost of toxic flow |
| **Fees** | maker_rebates − taker_fees | Net fee economics |

### Healthy PnL Profile
- **Spread capture >> adverse selection**: you're capturing more than losing
- **Inventory PnL ≈ 0**: you're not taking directional bets
- **Fees are net positive**: you're primarily a maker (earning rebates)
