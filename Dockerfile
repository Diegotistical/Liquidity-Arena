# ═══════════════════════════════════════════════════════════════════════
# Liquidity Arena — Multi-stage Docker Build
# ═══════════════════════════════════════════════════════════════════════
#
# Stage 1: Build C++ matching engine
# Stage 2: Python runtime + simulation
#
# Usage:
#   docker build -t liquidity-arena .
#   docker run -p 9876:9876 -p 8765:8765 -p 8080:8080 liquidity-arena

# ── Stage 1: Build C++ engine ────────────────────────────────────────
FROM gcc:13-bookworm AS builder

RUN apt-get update && apt-get install -y cmake git && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY CMakeLists.txt .
COPY engine/ engine/

# Build engine + tests.
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j$(nproc)

# Run C++ tests to verify build.
RUN ./build/arena_tests

# ── Stage 2: Runtime ─────────────────────────────────────────────────
FROM python:3.11-slim-bookworm

# Install C++ runtime libraries.
RUN apt-get update && apt-get install -y libstdc++6 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy built engine binary.
COPY --from=builder /build/build/arena_engine /app/arena_engine
COPY --from=builder /build/build/arena_bench /app/arena_bench

# Install Python dependencies (production only, no dev deps).
COPY pyproject.toml .
RUN pip install --no-cache-dir -e .

# Copy simulation code.
COPY simulation/ simulation/
COPY frontend/ frontend/

# Expose ports: engine (TCP), WebSocket, Frontend (HTTP).
EXPOSE 9876 8765 8080

# Default: start the C++ engine.
CMD ["./arena_engine", "--port", "9876"]
