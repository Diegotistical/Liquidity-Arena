# Market Model

## Event-Driven Architecture

The simulation uses a priority-queue event system instead of a time-step loop:

```
while events:
    event = pop()    # Lowest timestamp first
    process(event)   # Dispatch by event type
```

### Event Types

| Event | Description | Source |
|-------|-------------|--------|
| `MIDPRICE_MOVE` | True price update from price process | Scheduled at each dt |
| `AGENT_ACTION` | Agent generates quotes/orders | Scheduled with latency offset |
| `ORDER_CANCEL` | Stale quote cancellation | Agent-triggered |
| `BOOK_UPDATE` | LOB state broadcast | After each engine event |
| `METRICS_SAMPLE` | Periodic metrics recording | Every N steps |

### Why Event-Driven?

Time-step simulation treats all agents as synchronized — everyone sees the same state at time `t`. This is **unrealistic**:
- In real markets, faster agents see state changes first
- Latency differences create adverse selection
- Event-driven simulation makes latency *emerge naturally*

## Price Processes

### Ornstein-Uhlenbeck (Default)
```
dS = κ(θ − S)·dt + σ·dW
```
Mean-reverting process suitable for tick-level simulation.

### Regime-Switching Process

Markov-modulated volatility with 3 regimes:

| Regime | σ | Duration | Jump |
|--------|---|----------|------|
| CALM | 30 | ~200 steps | None |
| VOLATILE | 100 | ~50 steps | None |
| NEWS | 200 | ~10 steps | ±50 ticks |

Transition matrix:
```
         CALM  VOLATILE  NEWS
CALM  [  0.95   0.04    0.01  ]
VOL   [  0.10   0.85    0.05  ]
NEWS  [  0.30   0.50    0.20  ]
```

**Why this matters**: constant volatility is unrealistic. Real markets have calm periods punctuated by volatility bursts. The regime-switching model captures this, forcing strategies to adapt dynamically.

## Order Flow: Hawkes Process

Real order flow is NOT Poisson — it **clusters**. One aggressive order tends to trigger more aggressive orders.

### Self-Exciting Intensity
```
λ(t) = μ + Σ α·exp(-β·(t - tᵢ))
```

| Parameter | Default | Meaning |
|-----------|---------|---------|
| μ | 1.0 | Background intensity |
| α | 0.6 | Excitation amplitude |
| β | 1.5 | Decay rate |
| α/β | 0.4 | Branching ratio (< 1 for stationarity) |

### Stationarity Condition
α/β < 1 is required. When α/β → 1, the process becomes "critical" (explosive bursts).

### Microstructure Implications
- High Hawkes intensity → more noise orders → tighter spreads achievable
- Low Hawkes intensity → sparse flow → wider spreads needed
- Clustering creates short-term momentum that momentum traders exploit
