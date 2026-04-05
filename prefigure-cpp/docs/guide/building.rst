Building from Source
====================

Prerequisites
-------------

**Required:**

- C++23 compiler (GCC 13+, Clang 17+, or MSVC 19.36+)
- CMake 3.24+
- Internet connection (for FetchContent to download missing dependencies)

**Automatically fetched if not installed:**

- `Eigen 3.4+ <https://eigen.tuxfamily.org/>`_ — linear algebra (header-only)
- `spdlog <https://github.com/gabime/spdlog>`_ — logging (header-only)
- `Catch2 v3 <https://github.com/catchorg/Catch2>`_ — testing framework
- `pybind11 <https://github.com/pybind/pybind11>`_ — Python bindings

**Vendored (included in ``third_party/``):**

- `pugixml <https://pugixml.org/>`_ — XML parsing
- `exprtk <https://www.partow.net/programming/exprtk/>`_ — expression evaluation
- `cpp-base64 <https://github.com/ReneNyffenegger/cpp-base64>`_ — base64 encoding
- `rapidcsv <https://github.com/d99kris/rapidcsv>`_ — CSV parsing

**Optional (for full feature set):**

- Boost 1.74+ — for ODE solver (Boost.Odeint) and network graphs (Boost.Graph)
- `GEOS <https://libgeos.org/>`_ — for boolean geometry operations on shapes
- `libcairo <https://www.cairographics.org/>`_ — for text measurement in labels
- `liblouis <https://liblouis.io/>`_ — for braille translation in tactile output
- Node.js + npm — for MathJax mathematical label rendering

On Ubuntu/Debian::

    sudo apt-get install -y libeigen3-dev libspdlog-dev libboost-all-dev \
        libgeos-dev libcairo2-dev liblouis-dev pybind11-dev nodejs npm

Quick Build
-----------

::

    cd prefigure-cpp
    ./build.sh          # Release build (default)
    ./build.sh --debug  # Debug build

Or manually with CMake::

    cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel $(nproc)

CMake Options
-------------

All options default to ``ON``. Disable features you don't need:

.. list-table::
   :header-rows: 1
   :widths: 35 10 55

   * - Option
     - Default
     - Description
   * - ``PREFIGURE_BUILD_PYTHON``
     - ON
     - Build PyBind11 Python bindings
   * - ``PREFIGURE_BUILD_TESTS``
     - ON
     - Build Catch2 test suite
   * - ``PREFIGURE_WITH_DIFFEQS``
     - ON
     - ODE solver support (requires Boost.Odeint)
   * - ``PREFIGURE_WITH_NETWORK``
     - ON
     - Network graph support (requires Boost.Graph)
   * - ``PREFIGURE_WITH_SHAPES``
     - ON
     - Boolean geometry (requires GEOS)
   * - ``PREFIGURE_WITH_CAIRO``
     - ON
     - Cairo text measurement for labels
   * - ``PREFIGURE_WITH_LIBLOUIS``
     - ON
     - Braille translation for tactile output

Example — minimal build without optional dependencies::

    cmake -B build -S . \
        -DPREFIGURE_WITH_DIFFEQS=OFF \
        -DPREFIGURE_WITH_NETWORK=OFF \
        -DPREFIGURE_WITH_SHAPES=OFF \
        -DPREFIGURE_WITH_CAIRO=OFF \
        -DPREFIGURE_WITH_LIBLOUIS=OFF \
        -DPREFIGURE_BUILD_PYTHON=OFF

Running Tests
-------------

::

    cd prefigure-cpp
    ./build/tests/prefigure_tests

Or via CTest::

    cd build && ctest --output-on-failure

Build Artifacts
---------------

After building:

- ``build/libprefigure.a`` — static library
- ``build/tests/prefigure_tests`` — test executable
- ``build/_prefigure_cpp*.so`` — Python extension module (if Python bindings enabled)
