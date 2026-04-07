Profiling and Benchmarking
==========================

The ``profiling_comparison.py`` script at the repository root benchmarks the
Python and C++ backends end-to-end across the eight example diagrams shipped
with PreFigure.  For each diagram it runs the full ``parse()`` pipeline a
configurable number of times against each backend, computes the per-diagram
mean and standard deviation, and renders a side-by-side bar chart with the
speedup factor annotated above each pair.

The script exists so that any user can confirm on their own machine that the
C++ port is genuinely faster than the Python pipeline for the work it
actually does.  It is also the canonical entry point for diagnosing
performance regressions: every per-diagram timing is broken out, and an
optional cold-start column reveals the cost of the very first call before
any caches are warm.

.. note::

   The script bypasses ``prefig.engine.build()`` and dispatches each backend
   directly (``prefig.core.parse.parse`` for Python and ``_prefigure.parse``
   for C++) so the Python timing is never silently replaced by the C++
   backend through the engine's auto-detection.

Quick start
-----------

From the repository root, with the editable install built (``pip install -e .``)::

    .venv/bin/python profiling_comparison.py

That single command runs all 8 example diagrams, 30 timed runs per backend
per diagram, with a 1-run warmup discarded, and saves the bar chart to a
timestamped path under the temp directory.  Expected output structure::

    ==============================================================================
      PreFigure end-to-end performance comparison
    ==============================================================================
      examples dir : /home/sam/Documents/prefigure/prefig/resources/examples
      runs/diagram : 30  (warmup: 1)

    [OK] Python backend (prefig.core.parse) loaded
    [OK] C++ backend (prefig._prefigure) loaded

    [1/8] tangent.xml        Py=    5.4ms (±  0.5)  C++=    1.9ms (±  0.1)  speedup  2.86x
    [2/8] derivatives.xml    Py=    9.4ms (±  1.0)  C++=    6.5ms (±  0.2)  speedup  1.44x
    ... (one line per diagram) ...

    ==============================================================================================================
      Summary  (warm cache, n = 30 runs per diagram, times in milliseconds)
    ==============================================================================================================
      diagram                  Python μ  Python σ       C++ μ      C++ σ    speedup
      ----------------------------------------------------------------------------
      tangent                       5.4       0.5         1.9       0.1      2.86×
      ... (one row per diagram) ...
    ==============================================================================================================

    Plot saved to /tmp/prefigure-profile-20260407-114635.png
    Done.

The script returns immediately after saving the plot.  It does **not** open
an interactive window unless you explicitly pass ``--show`` (see below).
That makes it safe to run in CI and from non-interactive shells.

Profiling a single example
--------------------------

The default invocation runs all 8 examples sequentially, which takes a few
seconds.  When you only want to look at one diagram (or a small subset),
use ``--diagram`` / ``-d``.

Listing the available diagrams
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

::

    .venv/bin/python profiling_comparison.py --list

Output::

    Available example diagrams (in /home/sam/Documents/prefigure/prefig/resources/examples):
        tangent
        derivatives
        de-system
        diffeqs
        implicit
        projection
        riemann
        roots_of_unity

Running a single diagram
~~~~~~~~~~~~~~~~~~~~~~~~

::

    .venv/bin/python profiling_comparison.py --diagram tangent

The short form ``-d`` works identically::

    .venv/bin/python profiling_comparison.py -d tangent

Output (one progress line, one summary table row, one plot file)::

    [1/1] tangent.xml Py=    5.4ms (±  0.5)  C++=    1.9ms (±  0.1)  speedup  2.86x

    Summary  (warm cache, n = 30 runs per diagram, times in milliseconds)
      tangent                       5.4       0.5         1.9       0.1      2.86×

    Plot saved to /tmp/prefigure-profile-<timestamp>.png

Selecting multiple diagrams
~~~~~~~~~~~~~~~~~~~~~~~~~~~

Pass ``-d`` (or ``--diagram``) more than once.  Order does not matter; the
output is sorted::

    .venv/bin/python profiling_comparison.py -d diffeqs -d roots_of_unity

Suffix and case insensitivity
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All four of these are equivalent::

    .venv/bin/python profiling_comparison.py -d tangent
    .venv/bin/python profiling_comparison.py -d tangent.xml
    .venv/bin/python profiling_comparison.py -d Tangent
    .venv/bin/python profiling_comparison.py -d TANGENT.XML

Typo handling
~~~~~~~~~~~~~

Unknown diagram names produce a clean one-line error message and exit
status 1, with the list of valid names so you can correct yourself::

    $ .venv/bin/python profiling_comparison.py -d tagent
    ERROR: Unknown diagram name(s): tagent
    Available: de-system, derivatives, diffeqs, implicit, projection, riemann, roots_of_unity, tangent

Common combinations
~~~~~~~~~~~~~~~~~~~

Single diagram, fast iteration (5 runs instead of 30)::

    .venv/bin/python profiling_comparison.py -d tangent --runs 5

Single diagram with cold-start column::

    rm -rf ~/.cache/prefigure
    .venv/bin/python profiling_comparison.py -d tangent --runs 30 --report-cold

Single diagram, custom output path::

    .venv/bin/python profiling_comparison.py -d tangent --output /tmp/tangent.png

Single diagram, C++ only (skip the Python backend timing)::

    .venv/bin/python profiling_comparison.py -d tangent --no-python

Single diagram, also pop up the matplotlib window after saving::

    .venv/bin/python profiling_comparison.py -d tangent --show

Targeting options reference
---------------------------

Every CLI flag, in one place:

.. list-table::
   :header-rows: 1
   :widths: 22 12 14 52

   * - Flag
     - Type
     - Default
     - What it does
   * - ``--runs N``
     - int
     - ``30``
     - Number of timed runs per diagram per backend.
   * - ``--warmup N``
     - int
     - ``1``
     - Number of untimed warmup runs to discard before measuring.  The
       script prints a warning if you pass ``0``, because the first run
       always pays one-time MathJax startup costs.
   * - ``--output PATH``
     - str
     - none
     - Save the bar chart to this PNG path.  When omitted, the script
       saves to ``/tmp/prefigure-profile-<timestamp>.png`` and prints
       the path.
   * - ``--no-cpp``
     - flag
     - off
     - Skip the C++ backend (Python-only profiling).  Useful when you
       have not built the extension or want to profile only the Python
       pipeline.
   * - ``--no-python``
     - flag
     - off
     - Skip the Python backend (C++-only profiling).
   * - ``--report-cold``
     - flag
     - off
     - Also measure the cost of the very first call per diagram, before
       any warmup runs.  Adds two columns (``cold Py`` / ``cold C++``)
       to the summary table.  See below for interpretation.
   * - ``--diagram NAME`` / ``-d NAME``
     - str (repeatable)
     - all 8
     - Restrict the benchmark to the named diagram(s).  Suffix-insensitive
       (``tangent`` and ``tangent.xml`` are equivalent) and case-insensitive.
       Pass the flag multiple times to select several diagrams.
   * - ``--list``
     - flag
     - off
     - Print the available example diagrams and exit without running
       any benchmarks.
   * - ``--show``
     - flag
     - off
     - After saving the plot to disk, also open it in an interactive
       matplotlib window.  This call is **blocking** — the script
       waits for you to close the window before exiting.  Without this
       flag the script always saves and returns immediately.

The only constraint enforced by the script is that ``--no-cpp`` and
``--no-python`` cannot both be passed.  All other flag combinations are
valid; for example you can ``--no-python -d tangent --runs 5 --report-cold``
to profile a single diagram on the C++ backend only with cold-call
reporting and a quick 5-run pass.

Cold vs warm: understanding the measurements
--------------------------------------------

The script reports two distinct kinds of timing.  Knowing which is which
matters because they answer different questions.

**Warm cache (the bar chart and the** ``Python μ`` / ``C++ μ`` **columns)** —
the average of the ``--runs`` post-warmup samples.  The first run is
discarded by ``--warmup 1`` so MathJax startup costs are excluded.  This
is the right number to compare against Python because it isolates the
actual prefigure pipeline cost from external Node.js noise.  It tells you
how fast each backend is for a typical batched workload where the MathJax
cache is already populated.

**Cold first call (the** ``cold Py`` / ``cold C++`` **columns,
when** ``--report-cold`` **is passed)** — the cost of the very first call
per diagram, *before* warmup.  It includes MathJax daemon startup if the
daemon hasn't been spawned yet, and on-disk cache loading if the disk
cache is present.  This is what a user pays once per process for a fresh
install; subsequent calls within the same process are warm.

Three states of the disk cache produce dramatically different cold numbers
for the same diagram on the same machine.  Approximate numbers from a
recent dev-machine run with ``-d tangent --runs 30 --report-cold``:

.. list-table::
   :header-rows: 1

   * - Disk cache state
     - cold Py
     - cold C++
     - warm Py
     - warm C++
   * - Empty (``rm -rf ~/.cache/prefigure`` first)
     - ~420 ms
     - ~400 ms
     - ~5 ms
     - ~2 ms
   * - Partial (some labels cached from earlier runs)
     - ~30–60 ms
     - ~30–60 ms
     - ~5 ms
     - ~2 ms
   * - Fully populated
     - ~5 ms
     - ~3 ms
     - ~5 ms
     - ~2 ms

The warm numbers are essentially identical across all three states because
they're not measuring MathJax cost at all.

For an **honest first-time-user** measurement, wipe the disk cache before
each run::

    rm -rf ~/.cache/prefigure
    .venv/bin/python profiling_comparison.py --runs 30 --report-cold

For a **steady-state CI user** measurement, leave the cache intact::

    .venv/bin/python profiling_comparison.py --runs 30 --report-cold

Both are valid stories.  The bar chart (which always shows warm-cache
averages) is the same either way and is the right number to quote when
comparing the C++ port's actual computational speedup against Python.

Why the warm-cache numbers are so fast
--------------------------------------

When the bar chart shows single-digit milliseconds, that is *not* a
measurement bug — it's the result of three layers of caching that the
project added specifically so that production batch workflows do not pay
the MathJax startup cost on every call.

**1. In-process LaTeX → SVG cache.**  Each ``LocalMathLabels`` instance
shares a class-level dictionary keyed by the LaTeX text of every label
that has ever been rendered in the current process.  The first time
``\omega^3`` appears, it goes through MathJax once; every subsequent build
that contains ``\omega^3`` reuses the cached SVG.  The cache is populated
both by direct MathJax calls and by the on-disk cache loader described
next.  See ``prefig/core/label_tools.py`` (``LocalMathLabels._svg_cache``)
and ``prefigure-cpp/src/label_tools.cpp`` (``LocalMathLabels::s_svg_cache_``).

**2. On-disk cache shared across processes.**  At
``~/.cache/prefigure/mathjax-<format>-v<mathjax-version>.json`` lives a
plain-JSON file mapping LaTeX text to rendered SVG/braille strings.  Both
the Python and C++ backends read and write this file, so a build with one
backend warms the cache for the other.  The path is versioned by the
installed MathJax version so a MathJax upgrade automatically invalidates
every cached entry.  The file is loaded once per process per format and
written via an ``atexit`` hook.

**3. Persistent MathJax daemon.**  When the in-memory and on-disk caches
both miss, the cost of running MathJax for that label drops from ~700 ms
(spawning a fresh ``node mj-sre-page.js``) to ~10 ms by routing the request
through a long-running ``mj-sre-daemon.js`` process spawned lazily on the
first miss in the host process.  The daemon loads MathJax + SRE once at
startup, then reads JSON requests on stdin and writes JSON responses on
stdout.  See ``prefig/core/mj_sre/mj-sre-daemon.js`` for the daemon
implementation.

The combined effect: a batch of 100 builds with novel labels in one
Python process drops from ``100 × 700 ms = 70 s`` to about
``700 ms + 99 × 10 ms ≈ 1.7 s``.  A second batch on the same machine, even
in 100 separate processes, drops further to about ``100 × ~5 ms = 500 ms``
because the disk cache survives across processes.  These are the
realistic improvements the bar chart is showing.

Reading the bar chart
---------------------

Each x-axis position is one diagram.  Two bars per position:

- **Blue (left)** = Python backend
- **Orange (right)** = C++ backend

Each bar shows:

- **Bar height** = mean build time over the ``--runs`` post-warmup samples
- **Black tick** = ± one standard deviation
- **Number above the bar** = mean in milliseconds (for at-a-glance reading)
- **Green/red ratio above the pair** = ``Python_mean / C++_mean``, the
  speedup factor.  Green when the C++ backend is faster (the typical case),
  red when Python wins (rare).

The chart title always shows the active mode and sample count, e.g.::

    PreFigure end-to-end build time: Python vs C++  (warm cache, n=30 runs per diagram, mean ± stdev)

When ``--report-cold`` is passed, the summary table printed to the
terminal gains two extra columns (``cold Py`` and ``cold C++``); the bar
chart itself still shows warm-cache averages because the cold call is a
single sample with high variance and no error bars.

Output files
------------

The script always saves the plot to disk.  Three modes:

**1. Default (no** ``--output`` **).**  The plot is saved to a
timestamped path under the system temp directory and the path is printed
at the end of the run::

    Plot saved to /tmp/prefigure-profile-20260407-114635.png

**2.** ``--output PATH.png``.  The plot is saved exactly to that path::

    .venv/bin/python profiling_comparison.py --output prof.png
    # → Plot saved to prof.png

**3.** ``--show``.  In addition to saving (to either the temp path or
``--output``), the script opens the plot in an interactive matplotlib
window.  **This call is blocking** — the script waits for you to close
the window before exiting.  Use it only when you actually want to look
at the chart immediately::

    .venv/bin/python profiling_comparison.py --show
    .venv/bin/python profiling_comparison.py --output prof.png --show

The script never blocks unless ``--show`` is explicitly passed, which
makes it safe to run in CI and from non-interactive shells.

Troubleshooting
---------------

.. list-table::
   :header-rows: 1
   :widths: 35 35 30

   * - Symptom
     - Cause
     - Fix
   * - First diagram's ``cold`` column is ~400 ms instead of ~5 ms
     - The disk cache is empty AND the daemon needs to spawn.
     - Expected on a fresh install or after ``rm -rf ~/.cache/prefigure``.
       Subsequent diagrams in the same run will be fast.
   * - All ``cold`` columns are ~5–30 ms
     - The disk cache from a previous run is still populated.
     - This is the steady-state cost.  To see honest cold timings,
       ``rm -rf ~/.cache/prefigure`` before running.
   * - ``[--] C++ backend not available``
     - The ``_prefigure`` extension isn't built or installed.
     - From the repo root: ``pip install -e .``
   * - ``ModuleNotFoundError: No module named 'matplotlib'``
     - The dev extras aren't installed.
     - ``pip install -e ".[dev]"``
   * - Speedup ratios near ``1.0×`` across the board
     - You're running an older copy of ``profiling_comparison.py`` from
       before the in-process LaTeX cache landed, when MathJax startup
       dominated every call.
     - Pull the latest revision from ``cpp-packaging`` and rebuild the
       extension.
   * - Script appears to hang after the per-diagram progress line
     - You passed ``--show`` and matplotlib is waiting for you to
       close the interactive window.
     - Close the matplotlib window, or omit ``--show`` to make the
       script return immediately.

Related scripts
---------------

- ``./verify_tests.sh`` (repository root) — full regression suite: Python
  pytest in both backends, C++ Catch2 unit + integration tests, and an
  end-to-end SVG size diff comparing the two backends.  Run this after
  any change to confirm nothing has regressed.
- ``./prefigure-cpp/scripts/benchmark.sh`` — convenience wrapper that
  performs a fresh CMake Release build of the extension and then runs
  ``profiling_comparison.py``.  Useful when you've just changed C++ code
  and want a clean benchmark in one step.  Takes one optional positional
  argument for ``--runs`` (default ``5``).
- ``./correctness_comparison.py`` — compares the *structural and
  numerical equivalence* of the SVG output produced by the two backends,
  complementary to the speed comparison.  See the
  :doc:`testing` page for details.

Reproducible numbers from this round
------------------------------------

The following numbers were captured on the dev machine with a freshly
wiped disk cache and ``--runs 30 --report-cold``.  They are included as a
sanity check, not a performance promise: your numbers will differ
depending on hardware.

.. list-table::
   :header-rows: 1
   :widths: 25 15 15 20

   * - Diagram
     - Python μ (ms)
     - C++ μ (ms)
     - Speedup
   * - tangent
     - 5.4
     - 1.9
     - 2.86×
   * - derivatives
     - 9.4
     - 6.5
     - 1.44×
   * - de-system
     - 7.2
     - 2.1
     - 3.41×
   * - diffeqs
     - 28.3
     - 2.8
     - **10.04×**
   * - implicit
     - 99.7
     - 57.4
     - 1.74×
   * - projection
     - 5.7
     - 2.6
     - 2.21×
   * - riemann
     - 6.5
     - 2.7
     - 2.40×
   * - roots_of_unity
     - 8.6
     - 2.2
     - 3.86×

What matters in this table is not the absolute milliseconds (which depend
heavily on CPU, memory, and disk speed) but the **shape**: C++ is
consistently faster across all eight diagrams, and dramatically so for
compute-heavy workloads like ``diffeqs`` (a slope field with nine ODE
integrations via ``<repeat>``).  The reason ``derivatives`` and
``implicit`` show the smallest gap is that their bottleneck is the
quadtree subdivision and expression evaluation, where both backends spend
most of their time inside nested calls — the C++ exprtk engine is fast
but not 10× faster than CPython at that workload.

If your local numbers all sit at ~700 ms with speedups near ``1.0×``, the
in-process cache is not engaging.  Check that you have rebuilt the
extension (``pip install -e .``) and that the script you are running is
the current version.
