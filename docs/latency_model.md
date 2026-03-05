# Latency Model

## Why Latency Matters

In real markets, latency determines:
1. **Who gets filled**: faster agents see book changes first and can react
2. **Adverse selection costs**: slower MMs have stale quotes that get picked off
3. **Cancel success**: whether a cancel arrives before a fill (race condition)

## Agent Latency Profiles

| Agent | Latency | Jitter | Analogy |
|-------|---------|--------|---------|
| Latency Arb | 50µs | ±10µs | Co-located HFT |
| Market Maker | 200µs | ±50µs | Cross-connected MM |
| Informed Trader | 500µs | ±100µs | Institutional desk |
| Noise Trader | 1000µs | ±300µs | Retail/algo |
| Momentum Trader | 1000µs | ±300µs | Trend follower |

## Implementation

Each agent's events are scheduled with a latency offset in the event queue:

```python
for agent in agents:
    delay = agent.latency.sample_seconds()  # e.g., 200µs = 0.0002s
    event_queue.push(Event(
        timestamp = current_time + delay,
        event_type = AGENT_ACTION,
        agent_id = agent.id
    ))
```

This means a latency arb at 50µs sees the book **before** the MM at 200µs — enabling stale-quote pickup.

## Latency Spikes

With probability `spike_prob` (default: 0.1%), latency is multiplied by `spike_factor` (default: 10×). This models:
- GC pauses
- Network congestion
- OS scheduler jitter

## Impact on Strategy Economics

| MM Latency | Adverse Selection | Optimal Spread | Expected PnL |
|------------|-------------------|----------------|-------------|
| 50µs | Low | Tight | Higher |
| 200µs | Moderate | Medium | Moderate |
| 1000µs | High | Wide | Lower |

The relationship is non-linear: going from 200µs → 50µs provides disproportionate edge relative to 1000µs → 500µs.
