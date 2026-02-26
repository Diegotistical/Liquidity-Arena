# 🏟️ Liquidity Arena

[![CI](https://github.com/Diegotistical/Liquidity-Arena/actions/workflows/ci.yml/badge.svg)](https://github.com/Diegotistical/Liquidity-Arena/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**A sub-microsecond C++20 matching engine with Python adversarial market-making simulation and real-time browser visualization.**

<p align="center">
  <strong>C++20 · Price-Time Priority FIFO · 9.7ns Add Order · Object Pool Allocator · TCP Bridge · Avellaneda-Stoikov</strong>
</p>

---

## Architecture

```
┌─────────────┐    TCP (binary)    ┌──────────────────┐    WebSocket    ┌──────────────┐
│  C++ Engine  │◄──────────────────│ Python Simulation │────────────────►│  Browser UI  │
│  (LOB + ME)  │                   │  (Agents + Price) │                │ (Chart.js)   │
└─────────────┘                   └──────────────────┘                └──────────────┘
```

| Layer | Language | Purpose |
|-------|----------|---------|
| **Matching Engine** | C++20 | Limit order book, price-time priority matching, O(1) object pool |
| **TCP Bridge** | Binary protocol | Fixed-size message framing with memcpy serialization |
| **Simulation** | Python 3.11+ | Avellaneda-Stoikov MM + noise/informed/momentum adversaries |
| **Visualization** | HTML/JS/CSS | Real-time LOB depth, PnL, inventory gauge, spread, trade tape |

---

## Benchmark Results

All operations execute in **sub-microsecond** latency. Measured with Google Benchmark on 8-core 2.8GHz CPU:

| Operation | Latency | Throughput | Description |
|-----------|---------|------------|-------------|
| `BM_AddOrder` | **9.7 ns** | 103.8M/s | Non-crossing limit order insert |
| `BM_CancelOrder` | **53.8 ns** | 17.9M/s | Cancel existing order by ID |
| `BM_CrossingMatch` | **443 ns** | 2.2M/s | Limit order that crosses spread |
| `BM_MarketOrder` | **380 ns** | 3.0M/s | Market order with single fill |

---

## Key Design Decisions

### 🔢 Integer Tick Prices
All prices as `int64_t` ticks ($150.25 → `15025`). Zero floating-point on the hot path.

### 🧊 Zero Dynamic Allocation
`ObjectPool<Order, 1'000'000>` allocates once at startup. `allocate()` / `deallocate()` are O(1) via free list — **zero `new`/`delete` on the hot path**.

### 📊 Intrusive Linked List
Orders within `PriceLevel` use intrusive doubly-linked list pointers. O(1) insert, remove, FIFO pop.

### 🌐 TCP Socket Bridge
C++ ↔ Python via TCP sockets with binary protocol. Mimics real-world HFT network architecture.

### 📈 Avellaneda-Stoikov Optimal Quoting

$$r = s - q \cdot \gamma \cdot \sigma^2 \cdot (T-t)$$
$$\delta = \gamma \cdot \sigma^2 \cdot (T-t) + \frac{2}{\gamma} \cdot \ln\left(1 + \frac{\gamma}{\kappa}\right)$$

Where: `s` = midprice, `q` = inventory, `γ` = risk aversion, `σ²` = rolling variance, `κ` = arrival intensity.

---

## Quick Start

### Prerequisites
- **C++20 compiler** (GCC 12+, Clang 15+, or MSVC 2022)
- **CMake 3.20+**
- **Python 3.11+**

### Build & Test

```bash
# Configure and build
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4

# Run unit tests (38 tests)
./build/arena_tests

# Run latency benchmarks
./build/arena_bench
```

### Run Simulation

```bash
# Terminal 1: Start the C++ engine
./build/arena_engine --port 9876

# Terminal 2: Install deps & start simulation
pip install -r simulation/requirements.txt
python -m simulation.run_simulation

# Browser: http://localhost:8080
```

---

## Project Structure

```
Liquidity-Arena/
├── engine/
│   ├── include/
│   │   ├── types.hpp              # Integer tick system, type aliases
│   │   ├── order.hpp              # Order struct, intrusive list pointers
│   │   ├── object_pool.hpp        # O(1) heap-backed object pool
│   │   ├── price_level.hpp        # Intrusive doubly-linked list
│   │   ├── message.hpp            # Binary protocol (TLV framing)
│   │   ├── order_book.hpp         # LOB with unordered_map
│   │   ├── matching_engine.hpp    # FIFO price-time priority
│   │   └── tcp_server.hpp         # Winsock TCP, select() event loop
│   ├── src/
│   │   ├── order_book.cpp
│   │   ├── matching_engine.cpp
│   │   └── tcp_server.cpp
│   ├── tests/                     # 38 GTest unit tests
│   ├── bench/                     # Google Benchmark latency tests
│   └── main.cpp
├── simulation/
│   ├── agents/                    # 5 trading agents
│   │   ├── market_maker.py        # Avellaneda-Stoikov
│   │   ├── noise_trader.py
│   │   ├── informed_trader.py
│   │   └── momentum_trader.py
│   ├── market/                    # TCP client, price process, WS bridge
│   ├── config/default.yaml
│   ├── simulator.py
│   └── run_simulation.py
├── frontend/                      # Chart.js dashboard
├── .github/workflows/ci.yml       # CI: Linux GCC + Windows MSYS2
├── CMakeLists.txt
├── .clang-format
├── LICENSE
└── README.md
```

---

## Test Results

```
[==========] Running 38 tests from 4 test suites.
[  PASSED  ] 38 tests.

  ✓ ObjectPoolTest     (6 tests)    ✓ PriceLevelTest     (9 tests)
  ✓ OrderBookTest      (11 tests)   ✓ MatchingEngineTest (12 tests)
```

---

## Trading Agents

| Agent | Strategy | Order Flow |
|-------|----------|------------|
| **Avellaneda-Stoikov MM** | Inventory-penalized optimal quoting | Passive (limit) |
| **Noise Trader** | Random limit orders ± midprice | Mixed |
| **Informed Trader** | Exploits true price signal (toxic flow) | Aggressive (market) |
| **Momentum Trader** | EMA crossover trend following | Aggressive (market) |

---

## References

- Avellaneda, M. & Stoikov, S. (2008). *High-frequency trading in a limit order book*. Quantitative Finance, 8(3), 217–224.
- Guéant, O., Lehalle, C.-A., & Fernandez-Tapia, J. (2013). *Dealing with the inventory risk: A solution to the market making problem*. Mathematics and Financial Economics, 7, 477–507.
- Cartea, Á., Jaimungal, S., & Penalva, J. (2015). *Algorithmic and High-Frequency Trading*. Cambridge University Press.

---

## License

MIT — see [LICENSE](LICENSE).
