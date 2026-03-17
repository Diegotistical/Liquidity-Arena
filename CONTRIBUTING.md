# Contributing to Liquidity Arena

## Development Setup

### Prerequisites
- CMake ≥ 3.20
- GCC 13+ or Clang 17+ (Linux/macOS) / MSVC 2022+ (Windows)
- Python 3.11+
- Git

### Build the C++ Engine

For the most streamlined experience, use the provided build scripts:

**Linux / macOS:**
```bash
./build.sh
```

**Windows:**
```bat
.\build.bat
```

**Manual CMake Build:**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run tests
./build/arena_tests

# Run benchmarks
./build/arena_bench
```

### With Sanitizers (Debug)

```bash
cmake -B build-san -DARENA_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-san -j$(nproc)
./build-san/arena_tests
```

### Python Simulation

```bash
pip install -e ".[dev]"
python -m pytest simulation/tests/ -v
```

### Docker

```bash
docker-compose up
```

## Code Style

### C++
- C++20 standard
- `snake_case` for functions and variables
- `PascalCase` for types and classes
- All headers use `#pragma once`
- No dynamic allocation on the matching engine hot path
- Run with `-Werror` (all warnings are errors)

### Python
- Python 3.11+ with type hints
- Format with `ruff format`
- Lint with `ruff check`
- All agents inherit from `BaseAgent`

## Pull Request Checklist

- [ ] All C++ tests pass (`./build/arena_tests`)
- [ ] All Python tests pass (`pytest simulation/tests/ -v`)
- [ ] No new `-Werror` warnings
- [ ] Benchmarks show no regression
- [ ] New features have corresponding tests
- [ ] Documentation updated if applicable
