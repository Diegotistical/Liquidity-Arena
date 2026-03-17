#!/usr/bin/env bash
set -e

echo "=== Building Liquidity Arena (POSIX) ==="

mkdir -p build
cd build

echo "Configuring CMake..."
cmake .. -DCMAKE_BUILD_TYPE=Release

echo "Compiling..."
cmake --build . --config Release -j $(nproc)

echo "Build complete! Executables are in build/"
