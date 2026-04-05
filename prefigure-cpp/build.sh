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
        --clean)      rm -rf "$BUILD_DIR"; echo "Cleaned build directory" ;;
        --help)
            echo "Usage: ./build.sh [options]"
            echo "  --debug       Debug build"
            echo "  --release     Release build (default)"
            echo "  --no-python   Skip Python bindings"
            echo "  --no-tests    Skip test suite"
            echo "  --no-diffeqs  Skip ODE solver support"
            echo "  --no-network  Skip network graph support"
            echo "  --no-shapes   Skip boolean geometry support"
            echo "  --no-cairo    Skip Cairo text measurement"
            echo "  --no-braille  Skip braille translation"
            echo "  --clean       Remove build directory"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
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

if cmake --build "$BUILD_DIR" --target help 2>/dev/null | grep -q prefigure_tests; then
    echo "Running tests..."
    cd "$BUILD_DIR" && ctest --output-on-failure
fi

echo "Build complete."
