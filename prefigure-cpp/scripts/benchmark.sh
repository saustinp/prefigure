#!/bin/bash
set -e

# Build release with Python bindings and run benchmarks
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ROOT_DIR="$(dirname "$PROJECT_DIR")"

echo "=== PreFigure C++ Benchmark ==="
echo ""

# Build release
echo "Building release with Python bindings..."
cd "$PROJECT_DIR"
cmake -B build-bench -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DPREFIGURE_BUILD_PYTHON=ON \
    -DPREFIGURE_BUILD_TESTS=OFF
cmake --build build-bench --parallel "$(nproc)"

echo ""
echo "Running benchmarks..."
cd "$ROOT_DIR"

# Activate venv if available
source .venv/bin/activate 2>/dev/null || true

PYTHONPATH="$PROJECT_DIR/build-bench:$PYTHONPATH" \
    python3 profiling_comparison.py \
    --runs "${1:-5}" \
    --output benchmark_results.png

echo ""
echo "Results saved to benchmark_results.png"
