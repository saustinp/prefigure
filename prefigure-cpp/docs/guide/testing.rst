Testing
=======

The project uses three levels of testing: C++ unit tests, C++ integration
tests, and Python correctness comparison against the reference Python
implementation.

C++ Unit Tests (Catch2)
-----------------------

Unit tests live in ``tests/`` and are built with `Catch2 v3 <https://github.com/catchorg/Catch2>`_.

**Running all tests**::

    ./build/tests/prefigure_tests

**Running specific test tags**::

    ./build/tests/prefigure_tests "[calculus]"
    ./build/tests/prefigure_tests "[ctm]"
    ./build/tests/prefigure_tests "[math_utilities]"
    ./build/tests/prefigure_tests "[user_namespace]"
    ./build/tests/prefigure_tests "[integration]"
    ./build/tests/prefigure_tests "[pipeline]"

**Current test files:**

.. list-table::
   :header-rows: 1

   * - File
     - Tag
     - What it tests
   * - ``test_calculus.cpp``
     - ``[calculus]``
     - Richardson extrapolation derivatives vs analytical
   * - ``test_math_utilities.cpp``
     - ``[math_utilities]``
     - Vector ops, Bezier, line intersection, indicators
   * - ``test_ctm.cpp``
     - ``[ctm]``
     - Identity, translate, rotate, scale, compose, inverse
   * - ``test_user_namespace.cpp``
     - ``[user_namespace]``
     - Constants, arithmetic, functions, vectors, colors
   * - ``test_diagram_integration.cpp``
     - ``[integration]``
     - XML parsing, full pipeline for example diagrams

C++ Integration Tests
---------------------

Pipeline tests load real XML example files, run them through the full
``parse()`` pipeline, and verify the output SVG:

- Is well-formed XML (parseable by pugixml)
- Has an ``<svg>`` root with ``width``, ``height``, ``viewBox``
- Contains ``<defs>`` with at least one clippath
- Has a non-trivial number of graphical elements

Example test (from ``test_diagram_integration.cpp``)::

    TEST_CASE("Full pipeline: tangent.xml produces SVG output", "[pipeline]") {
        prefigure::parse(xml_path, prefigure::OutputFormat::SVG,
                         "", false, prefigure::Environment::PfCli);
        REQUIRE(std::filesystem::exists(output_path));
        // ... verify SVG structure ...
    }

Python Correctness Comparison
-----------------------------

The ``correctness_comparison.py`` script (in the project root) runs all 8
example XML files through both the Python and C++ backends and compares the
SVG output for structural equivalence.

**Running it**::

    source .venv/bin/activate
    python3 correctness_comparison.py

**What it compares:**

- Element tags (must match exactly)
- Attribute values (numeric attributes compared with tolerance)
- Path ``d`` data (tokenized, commands exact, numbers with tolerance)
- Transform attributes (numeric values with tolerance)
- Style properties (split and compared individually)
- Skips volatile attributes: ``id``, ``clip-path``, ``href`` (generated IDs differ)

**Options**::

    python3 correctness_comparison.py --tolerance 1e-2   # looser tolerance
    python3 correctness_comparison.py -v                 # verbose diffs
    python3 correctness_comparison.py --python-only      # just validate Python
    python3 correctness_comparison.py --examples tangent.xml  # specific files

Performance Benchmarking
------------------------

For benchmarking the Python vs C++ backends end-to-end across the eight
example diagrams — with full coverage of single-diagram targeting,
cold-vs-warm reporting, and the MathJax cache architecture — see the
dedicated :doc:`profiling` guide.

Adding a New Test
-----------------

**Unit test**: Add a ``TEST_CASE`` to an existing test file or create a new
``.cpp`` file and add it to ``tests/CMakeLists.txt``::

    add_executable(prefigure_tests
        ...
        test_my_new_module.cpp
    )

**Pipeline test**: Add a new ``TEST_CASE`` to ``test_diagram_integration.cpp``
that loads an XML file, runs ``prefigure::parse()``, and checks the output.

**Golden file test** (future): Generate reference SVGs from Python, store in
``tests/golden/``, and compare C++ output numerically.

Sanitizers (ASAN / UBSan)
--------------------------

The project supports AddressSanitizer and UndefinedBehaviorSanitizer for
detecting memory errors and undefined behavior at runtime::

    cmake -B build -S . -DPREFIGURE_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel
    ./build/tests/prefigure_tests

This adds ``-fsanitize=address,undefined -fno-omit-frame-pointer -g`` to all
compile and link flags.  Any memory errors (use-after-free, buffer overflow,
stack overflow) or undefined behavior (signed overflow, null dereference,
alignment violations) will be reported at runtime with a stack trace.

.. note::

   ASAN adds significant overhead.  Use a Debug build for sanitizer runs
   and a Release build for performance testing.

Code Coverage
--------------

To generate code coverage reports::

    cmake -B build -S . -DPREFIGURE_COVERAGE=ON -DCMAKE_BUILD_TYPE=Debug
    cmake --build build --parallel
    ./build/tests/prefigure_tests
    # Generate report with lcov:
    lcov --capture --directory build --output-file coverage.info
    genhtml coverage.info --output-directory coverage_report

This adds ``--coverage -fprofile-arcs -ftest-coverage`` to all compile and
link flags.  After running the tests, ``.gcda`` and ``.gcno`` files are
produced alongside the object files for processing by lcov/gcov.

Golden File Testing
--------------------

For high-confidence regression testing, golden file tests compare C++ SVG
output against reference SVGs generated by the Python implementation:

1. Generate reference SVGs from Python for each example XML file
2. Store in ``tests/golden/``
3. Run C++ pipeline on the same inputs
4. Compare structurally (element-by-element) with numeric tolerance

The ``correctness_comparison.py`` script provides this workflow without
storing golden files — it generates both outputs on the fly.  For CI
environments where the Python backend may not be available, pre-generated
golden files can be committed to the repository.

Test Resources
--------------

Example XML files are copied to ``tests/resources/`` at build time. They
come from ``prefig/resources/examples/`` in the Python project:

- ``tangent.xml`` — graph + tangent line + point
- ``derivatives.xml`` — function + first/second derivatives
- ``de-system.xml`` — system of ODEs
- ``diffeqs.xml`` — slope field + solution curves
- ``implicit.xml`` — implicit curves (level sets)
- ``projection.xml`` — vector projection
- ``riemann.xml`` — Riemann sum approximation
- ``roots_of_unity.xml`` — complex roots of unity on unit circle
