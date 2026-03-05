# 🏟️ Liquidity Arena

[![CI](https://github.com/Diegotistical/Liquidity-Arena/actions/workflows/ci.yml/badge.svg)](https://github.com/Diegotistical/Liquidity-Arena/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Python 3.11+](https://img.shields.io/badge/Python-3.11%2B-green.svg)](https://www.python.org/)

**A production-grade microstructure simulator with a sub-microsecond C++20 matching engine, event-driven multi-agent simulation, and real-time browser visualization.**

<p align="center">
  <strong>Price-Time Priority FIFO · Iceberg Orders · Maker/Taker Fees · Hawkes Order Flow · Regime Switching · Latency Arbitrage · Avellaneda-Stoikov</strong>
</p>

---

## Why This Exists

Most "market-making simulators" simulate **price**, not **the order book**. Price is a derived variable. Microstructure lives in the book.

Liquidity Arena simulates the actual order book: queue position, partial fills, hidden liquidity, maker/taker fee economics, per-agent latency, and adversarial multi-agent dynamics. If a quant at a prop shop opens this repo, they should see that you understand how markets actually work under the hood.

---

## Architecture

```
┌──────────────────┐    TCP (binary)    ┌──────────────────────────┐    WebSocket    ┌──────────────┐
│  C++ Engine v2.0 │◄──────────────────│  Python Event-Driven Sim  │────────────────►│  Browser UI  │
│  LOB + ME + Fees │                   │  Agents + Hawkes + Latency │                │  Chart.js    │
└──────────────────┘                   └──────────────────────────┘                └──────────────┘
```

| Layer | Language | Key Features |
|-------|----------|-------------|
| **Matching Engine** | C++20 | LOB, price-time FIFO, iceberg orders, modify, maker/taker fees, O(1) object pool |
| **TCP Bridge** | Binary TLV | Fixed-size message framing, MSVC + GCC/Clang compatible |
| **Simulation** | Python 3.11+ | Event-driven, A-S market maker, 5 adversarial agents, Hawkes order flow, regime switching, per-agent latency |
| **Metrics** | Python | Sharpe, adverse selection, fill ratio, PnL decomposition, inventory variance |
| **Visualization** | HTML/JS/CSS | Real-time LOB depth, PnL, inventory, spread, regime indicator, agent activity, trade tape |

---

## Engine Features

### Order Types
- **Limit orders** — price-time priority FIFO with partial fills
- **Market orders** — walk the book, unfilled remainder killed
- **Iceberg orders** — hidden liquidity with auto-replenish (display/hidden split)

### Order Lifecycle
- **Add** → rests on book or matches immediately
- **Cancel** → O(1) removal via intrusive linked list
- **Modify** → quantity down preserves queue position; price change = cancel+re-add

### Fee Model
Maker/taker fees in basis points (configurable):
- Default: 2 bps maker rebate, 3 bps taker fee (matching US equity exchanges)

### Queue Position Tracking
`queue_volume_ahead()` computes volume ahead of any order — critical for realistic fill modeling.

### Strategy API
Plug in custom C++ strategies via the abstract `Strategy` base class:
```cpp
class Strategy {
    virtual Quote generate_quote(const MarketState& state) = 0;
    virtual void on_fill(const FillMsg& fill) = 0;
};
```

---

## Simulation Features

### Event-Driven Architecture
Priority-queue event system replaces naive time-step loops. Per-agent latency offsets mean faster agents genuinely see book changes first.

### Price Processes
| Process | Description |
|---------|-------------|
| Ornstein-Uhlenbeck | Mean-reverting (default for tick simulation) |
| GBM | Geometric Brownian Motion |
| **Regime Switching** | 3-regime Markov chain (calm → volatile → news) with jumps |

### Hawkes Process Order Flow
Self-exciting point process: `λ(t) = μ + Σ α·exp(-β·(t - tᵢ))`
- Real order flow clusters; Hawkes captures this naturally
- Configurable branching ratio (α/β < 1 for stationarity)

### Multi-Agent Environment
| Agent | Strategy | Latency | Flow |
|-------|----------|---------|------|
| **Avellaneda-Stoikov MM** | Inventory-penalized optimal quoting | 200µs | Passive |
| **Noise Trader** ×3 | Random limit orders ± mid | 1ms | Mixed |
| **Informed Trader** | Exploits true price signal (toxic flow) | 500µs | Aggressive |
| **Momentum Trader** | EMA crossover trend following | 1ms | Aggressive |
| **Latency Arbitrageur** | Picks off stale quotes | **50µs** | Aggressive |

### Quantitative Metrics
| Metric | What It Measures |
|--------|-----------------|
| Sharpe Ratio | Risk-adjusted returns |
| Adverse Selection | Toxic flow cost (THE key MM metric) |
| Fill Ratio | Execution quality |
| Inventory Variance | Risk control |
| Quote Lifetime | Cancellation speed |
| Order-to-Trade Ratio | MM efficiency |
| **PnL Decomposition** | Spread capture + inventory − adverse selection − fees |

---

## Benchmark Results

All operations execute in **sub-microsecond** latency:

| Operation | Latency | Throughput | Description |
|-----------|---------|------------|-------------|
| `BM_AddOrder` | **9.7 ns** | 103.8M/s | Non-crossing limit order insert |
| `BM_CancelOrder` | **53.8 ns** | 17.9M/s | Cancel existing order by ID |
| `BM_CrossingMatch` | **443 ns** | 2.2M/s | Limit order that crosses spread |
| `BM_MarketOrder` | **380 ns** | 3.0M/s | Market order with single fill |

---

## Quick Start

### Prerequisites
- **C++20 compiler** — GCC 13+, Clang 17+, or MSVC 2022
- **CMake 3.20+**
- **Python 3.11+**

### Build & Test

```bash
# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run C++ unit tests (51 tests)
./build/arena_tests

# Run latency benchmarks
./build/arena_bench

# Run Python tests (39 tests)
pip install -e ".[dev]"
python -m pytest simulation/tests/ -v
```

### With Sanitizers

```bash
cmake -B build-san -DARENA_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-san -j$(nproc)
./build-san/arena_tests
```

### Run Simulation

```bash
# Terminal 1: Start the C++ engine
./build/arena_engine --port 9876

# Terminal 2: Start the event-driven simulation
python -m simulation.simulator --config simulation/config/default.yaml

# Browser: http://localhost:8080
```

### Docker

```bash
docker-compose up
```

---

## Project Structure

```
Liquidity-Arena/
├── engine/
│   ├── include/
│   │   ├── types.hpp              # Tick system, OrderType::ICEBERG, FeeSchedule, RejectReason
│   │   ├── order.hpp              # Order struct, display_qty/hidden_qty for icebergs
│   │   ├── object_pool.hpp        # O(1) free-list allocator (1M orders, zero heap alloc)
│   │   ├── price_level.hpp        # Intrusive linked list, queue_volume_ahead()
│   │   ├── message.hpp            # Binary TLV, MSVC pack macros, ModifyOrderMsg
│   │   ├── order_book.hpp         # LOB — add, cancel, modify, iceberg replenish
│   │   ├── matching_engine.hpp    # FIFO matching, iceberg handling, fee calculation
│   │   ├── tcp_server.hpp         # TCP server, broadcast, reject with reason codes
│   │   └── strategy.hpp           # Strategy API — Quote, MarketState, abstract base
│   ├── src/                       # Implementation files
│   ├── tests/                     # 51 GTest unit tests
│   ├── bench/                     # Google Benchmark latency suite
│   └── main.cpp
├── simulation/
│   ├── agents/                    # 5 trading agents
│   │   ├── market_maker.py        # Avellaneda-Stoikov with cancel-and-replace
│   │   ├── noise_trader.py        # Random liquidity provider
│   │   ├── informed_trader.py     # True price signal exploiter
│   │   ├── momentum_trader.py     # EMA crossover trend follower
│   │   └── latency_arb.py         # Stale-quote picker (50µs latency)
│   ├── market/
│   │   ├── tcp_client.py          # Binary TCP client
│   │   ├── price_process.py       # GBM, OU, RegimeSwitching, HawkesProcess
│   │   ├── latency_model.py       # Per-agent latency with jitter + spikes
│   │   └── ws_bridge.py           # WebSocket bridge to browser
│   ├── tests/                     # 39 pytest tests
│   ├── metrics.py                 # Sharpe, adverse selection, PnL decomposition
│   ├── simulator.py               # Event-driven simulation core
│   └── config/default.yaml        # Full simulation config
├── frontend/                      # Chart.js premium dashboard
├── docs/                          # Exchange-spec documentation
│   ├── matching_engine.md         # Order types, matching rules, memory model
│   ├── market_model.md            # Event architecture, Hawkes, regime switching
│   ├── agents.md                  # A-S equations, agent strategies
│   ├── latency_model.md           # Per-agent profiles, economic impact
│   ├── metrics.md                 # Metric formulas, PnL decomposition
│   └── strategy_api.md            # C++ and Python interfaces
├── .github/workflows/ci.yml       # CI: Linux, Windows, sanitizers, Python
├── Dockerfile                     # Multi-stage: GCC 13 → Python 3.11
├── docker-compose.yml             # 3-service orchestration
├── pyproject.toml                 # Python packaging + ruff + pytest
├── CMakeLists.txt                 # v2.0, MSVC support, sanitizer option
├── CONTRIBUTING.md                # Build instructions, PR checklist
├── CHANGELOG.md                   # v2.0.0 release notes
└── README.md
```

---

## Test Results

```
C++ Engine:  51 tests from 4 suites  ✓ PASSED
Python Sim:  39 tests from 3 suites  ✓ PASSED

  ✓ ObjectPoolTest       (6 tests)     ✓ PriceLevelTest      (9 tests)
  ✓ OrderBookTest       (11 tests)     ✓ MatchingEngineTest (25 tests)
  ✓ TestNoiseTrader      (5 tests)     ✓ TestInformedTrader  (4 tests)
  ✓ TestMomentumTrader   (2 tests)     ✓ TestLatencyArb      (3 tests)
  ✓ TestMetricsEngine   (11 tests)     ✓ TestPriceProcesses (14 tests)
```

---

## Key Design Decisions

### 🔢 Integer Tick Prices
All prices as `int64_t` ticks ($150.25 → `15025`). Zero floating-point on the hot path.

### 🧊 Zero Dynamic Allocation
`ObjectPool<Order, 1'000'000>` allocates once at startup. `allocate()` / `deallocate()` are O(1) via free list — **zero `new`/`delete` on the hot path**.

### 📊 Intrusive Linked List
Orders within `PriceLevel` use intrusive doubly-linked list pointers. O(1) insert, remove, FIFO pop.

### 🌐 Event-Driven Simulation
Priority-queue event system with per-agent latency offsets. Faster agents genuinely see book changes first — latency advantage emerges naturally.

### 📈 Avellaneda-Stoikov Optimal Quoting

$$r = s - q \cdot \gamma \cdot \sigma^2 \cdot (T-t)$$
$$\delta = \gamma \cdot \sigma^2 \cdot (T-t) + \frac{2}{\gamma} \cdot \ln\left(1 + \frac{\gamma}{\kappa}\right)$$

Where: `s` = midprice, `q` = inventory, `γ` = risk aversion, `σ²` = rolling variance, `κ` = arrival intensity.

### 🧊 Iceberg Orders
Hidden liquidity with display/hidden split. Auto-replenish sends order to back of queue (loses time priority).

### 💰 Maker/Taker Fee Economics
Per-fill fee calculation in basis points. Net fee economics are part of PnL decomposition.

---

## Documentation

Detailed technical docs in [`docs/`](docs/) — reads like a mini exchange specification:
- [Matching Engine](docs/matching_engine.md) — order types, matching rules, memory model, protocol
- [Market Model](docs/market_model.md) — event architecture, Hawkes process, regime switching
- [Agents](docs/agents.md) — A-S equations, agent strategies, parameters
- [Latency Model](docs/latency_model.md) — per-agent profiles, economic impact
- [Metrics](docs/metrics.md) — metric formulas, PnL decomposition
- [Strategy API](docs/strategy_api.md) — C++ and Python interfaces

---

## References

- Avellaneda, M. & Stoikov, S. (2008). *High-frequency trading in a limit order book*. Quantitative Finance, 8(3), 217–224.
- Guéant, O., Lehalle, C.-A., & Fernandez-Tapia, J. (2013). *Dealing with the inventory risk*. Mathematics and Financial Economics, 7, 477–507.
- Cartea, Á., Jaimungal, S., & Penalva, J. (2015). *Algorithmic and High-Frequency Trading*. Cambridge University Press.
- Hawkes, A.G. (1971). *Spectra of some self-exciting and mutually exciting point processes*. Biometrika, 58(1), 83–90.

---

## License

MIT — see [LICENSE](LICENSE).
