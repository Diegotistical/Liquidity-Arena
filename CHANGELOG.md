# Changelog

All notable changes to this project will be documented in this file.

## [2.0.0] — 2026-03-05

### Added — Core Engine
- **Order Modify**: Cancel+re-add for price changes, in-place for qty down (preserves queue position)
- **Iceberg Orders**: Hidden liquidity with display/hidden split and automatic replenish
- **Maker/Taker Fees**: Configurable fee schedule (basis points) applied per fill
- **Queue Position Tracking**: `queue_volume_ahead()` computes volume ahead in queue
- **Strategy API**: `strategy.hpp` — abstract C++ interface for pluggable strategies
- **Reject Reasons**: Proper `RejectReason` enum replacing magic uint8_t
- **MSVC Portability**: Cross-platform `ARENA_PACK_BEGIN/END` macros for struct packing
- **Sanitizer Support**: `ARENA_SANITIZERS` CMake option for ASan + UBSan

### Added — Simulation
- **Event-Driven Architecture**: Priority-queue event system replacing time-step loop
- **Hawkes Process**: Self-exciting order flow with configurable μ, α, β parameters
- **Regime Switching**: Markov-modulated price process (calm/volatile/news regimes)
- **Latency Model**: Per-agent latency with jitter and spike simulation
- **Latency Arb Agent**: Picks off stale quotes, demonstrates latency advantage
- **Quantitative Metrics**: Sharpe, adverse selection, fill ratio, PnL decomposition, OTR

### Added — Infrastructure
- Python test suite (pytest): 39 tests across agents, metrics, price processes
- `pyproject.toml` with proper Python packaging and ruff configuration
- `Dockerfile` — multi-stage build (GCC 13 → Python 3.11 slim)
- `docker-compose.yml` — orchestrated engine/simulation/frontend services
- `CONTRIBUTING.md` — build instructions, code style, PR checklist

### Fixed
- `.gitignore` UTF-16 null byte corruption on lines 39-40
- MSVC build compatibility (conditional `-Werror` / `/W4 /WX`)

## [1.0.0] — Initial Release

- C++ matching engine with price-time FIFO priority
- Object pool for zero-allocation order management
- Intrusive linked-list price levels
- Binary TCP protocol for engine-client communication
- Python simulation with Avellaneda-Stoikov market maker
- Noise, Informed, and Momentum trader agents
- WebSocket bridge + Chart.js frontend
- Google Test suite (38 tests)
- Google Benchmark latency suite
