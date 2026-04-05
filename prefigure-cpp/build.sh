#!/bin/bash
set -e

# PreFigure C++ build script
#
# Prerequisites (Ubuntu/Debian):
#   sudo apt-get install -y libeigen3-dev libspdlog-dev libboost-all-dev \
#       libgeos-dev libcairo2-dev liblouis-dev pybind11-dev
#
# Missing deps will be fetched automatically via CMake FetchContent.

BUILD_DIR="build"
BUILD_TYPE="Release"
CMAKE_ARGS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)      BUILD_TYPE="Debug" ;;
        --release)    BUILD_TYPE="Release" ;;
        --no-python)  CMAKE_ARGS+=" -DPREFIGURE_BUILD_PYTHON=OFF" ;;
        --no-tests)   CMAKE_ARGS+=" -DPREFIGURE_BUILD_TESTS=OFF" ;;
        --no-diffeqs) CMAKE_ARGS+=" -DPREFIGURE_WITH_DIFFEQS=OFF" ;;
        --no-network) CMAKE_ARGS+=" -DPREFIGURE_WITH_NETWORK=OFF" ;;
        --no-shapes)  CMAKE_ARGS+=" -DPREFIGURE_WITH_SHAPES=OFF" ;;
        --no-cairo)   CMAKE_ARGS+=" -DPREFIGURE_WITH_CAIRO=OFF" ;;
        --no-braille) CMAKE_ARGS+=" -DPREFIGURE_WITH_LIBLOUIS=OFF" ;;
        --asan)
            BUILD_TYPE="Debug"
            CMAKE_ARGS+=" -DPREFIGURE_SANITIZE=ON"
            echo "Note: --asan enables both AddressSanitizer and UBSan"
            ;;
        --ubsan)
            BUILD_TYPE="Debug"
            CMAKE_ARGS+=" -DPREFIGURE_SANITIZE=ON"
            echo "Note: --ubsan enables both AddressSanitizer and UBSan"
            ;;
        --sanitize)
            BUILD_TYPE="Debug"
            CMAKE_ARGS+=" -DPREFIGURE_SANITIZE=ON"
            ;;
        --coverage)
            BUILD_TYPE="Debug"
            CMAKE_ARGS+=" -DPREFIGURE_COVERAGE=ON"
            ;;
        --bench)
            BUILD_TYPE="Release"
            CMAKE_ARGS+=" -DPREFIGURE_BUILD_PYTHON=ON"
            ;;
        --golden)
            echo "Generating golden files from Python implementation..."
            cd "$(dirname "$0")/.."
            source .venv/bin/activate 2>/dev/null || true
            python3 prefigure-cpp/scripts/generate_golden.py
            exit 0
            ;;
        --clean)
            rm -rf "$BUILD_DIR"
            echo "Cleaned build directory"
            exit 0
            ;;
        --help)
            echo "Usage: ./build.sh [options]"
            echo ""
            echo "Build modes:"
            echo "  --debug       Debug build (default for sanitizers)"
            echo "  --release     Release build (default)"
            echo ""
            echo "Features:"
            echo "  --no-python   Skip Python bindings"
            echo "  --no-tests    Skip test suite"
            echo "  --no-diffeqs  Skip ODE solver support"
            echo "  --no-network  Skip network graph support"
            echo "  --no-shapes   Skip boolean geometry support"
            echo "  --no-cairo    Skip Cairo text measurement"
            echo "  --no-braille  Skip braille translation"
            echo ""
            echo "Testing & Quality:"
            echo "  --asan        Build with AddressSanitizer"
            echo "  --ubsan       Build with UndefinedBehaviorSanitizer"
            echo "  --sanitize    Build with ASAN + UBSan"
            echo "  --coverage    Build with code coverage (gcov)"
            echo "  --bench       Release build with Python bindings for benchmarking"
            echo "  --golden      Generate golden SVG files from Python"
            echo ""
            echo "Maintenance:"
            echo "  --clean       Remove build directory"
            echo "  --help        Show this help"
            exit 0
            ;;
        *)
            echo "Unknown option: $1 (use --help for usage)"
            exit 1
            ;;
    esac
    shift
done

echo "Configuring PreFigure C++ (${BUILD_TYPE})..."
cmake -B "$BUILD_DIR" -S . \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    $CMAKE_ARGS

echo "Building..."
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

# Run tests if built
if [[ -f "$BUILD_DIR/tests/prefigure_tests" ]]; then
    echo ""
    echo "Running tests..."
    "$BUILD_DIR/tests/prefigure_tests"
fi

# If coverage was enabled, report
if [[ "$CMAKE_ARGS" == *"COVERAGE"* ]]; then
    echo ""
    echo "Generating coverage report..."
    if command -v lcov &> /dev/null; then
        lcov --capture --directory "$BUILD_DIR" --output-file "$BUILD_DIR/coverage.info" \
            --exclude '*/third_party/*' --exclude '*/build/*' --exclude '/usr/*'
        echo "Coverage data: $BUILD_DIR/coverage.info"
        echo "View with: genhtml $BUILD_DIR/coverage.info -o $BUILD_DIR/coverage_html"
    else
        echo "lcov not installed — raw gcov data in $BUILD_DIR"
    fi
fi

echo ""
echo "Build complete."
