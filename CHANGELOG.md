# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.1.0] - 2026-03-24
### Fixed
- **CRITICAL**: Fixed `on_fill()` signature mismatch where fills were silently dropping in the simulator.
- **CRITICAL**: Fixed market maker order accumulation by implementing a proper cancel-and-replace loop.
- **CRITICAL**: Fixed order ID collisions across different simulation agents by adding 1M ID block offsets per agent.
- **Protocol**: Added missing `display_qty` field to Python `NewOrderMsg` serialization loop.
- **Engine**: Added adaptive CPU backoff yielding/sleep to engine TCP spin loop.
- **C++20**: Replaced deprecated `std::aligned_storage_t` with `alignas(T) std::byte` in `ObjectPool`.
- **Docker**: Removed dev dependencies (`pytest`, `ruff`) from production runtime container context.
- **Tests**: Re-wrote mock boundaries in unit tests to allow parallel/hybrid execution without module collision.

### Added
- **Tests**: Created comprehensive Python integration test suite covering end-to-end TCP loop and agent dynamics (matching priorities, cancellations).
- **Frontend**: Wired up `--ws` and `--ws-port` CLI flags to natively support connecting the frontend WebSocket bridge.
- `examples/` directory for sample reference scripts.

## [2.0.0] - 2026-03-10
### Added
- Complete rewrite of C++ matching engine.
- Zero-copy lock-free ring buffer for thread handoff.
- Intrusive limit order book.
- Initial Python multi-agent event loop.
