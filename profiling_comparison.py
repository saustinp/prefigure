#!/usr/bin/env python3
"""
PreFigure: Python vs C++ end-to-end performance comparison.

For each of the 8 example XML diagrams, this script runs the full build
pipeline a configurable number of times against both backends, computes
per-diagram averages, and renders a side-by-side bar chart of the results.

The two backends are dispatched directly (no auto-selection):

    Python:  prefig.core.parse.parse(filename, "svg", None, False, "pf_cli")
    C++:     _prefigure.parse(filename, _prefigure.OutputFormat.SVG, "",
                              False, _prefigure.Environment.PfCli)

This bypasses ``prefig.engine.build()`` so the Python timing isn't
contaminated by the engine's automatic backend autodetection (which
silently switches to C++ whenever the extension is importable).

USAGE
-----
From the repository root, with the editable install built::

    # Run all 8 examples, default 30 runs each, both backends, save the chart
    # to a timestamped file under /tmp/ and print its path:
    .venv/bin/python profiling_comparison.py

    # List the available example diagrams and exit:
    .venv/bin/python profiling_comparison.py --list

    # Profile a single diagram in isolation (suffix-insensitive,
    # case-insensitive):
    .venv/bin/python profiling_comparison.py --diagram tangent
    .venv/bin/python profiling_comparison.py -d tangent.xml

    # Multiple diagrams at once:
    .venv/bin/python profiling_comparison.py -d diffeqs -d roots_of_unity

    # Honest first-time-user measurement (wipe disk cache, report cold call):
    rm -rf ~/.cache/prefigure
    .venv/bin/python profiling_comparison.py --runs 30 --report-cold

    # Save the chart to a specific path instead of /tmp/:
    .venv/bin/python profiling_comparison.py --runs 30 --output prof.png

    # Skip a backend (useful when one isn't built):
    .venv/bin/python profiling_comparison.py --no-cpp     # Python only
    .venv/bin/python profiling_comparison.py --no-python  # C++ only

    # Increase or decrease the warmup discard count:
    .venv/bin/python profiling_comparison.py --warmup 2   # discard first 2

    # ALSO open the matplotlib window (blocking — script waits for the
    # window to close).  Without this flag the script always saves the
    # chart and returns immediately, which is what you want for CI use:
    .venv/bin/python profiling_comparison.py --show

REQUIREMENTS
------------
* The Python ``prefig`` package importable (``pip install -e .``).
* The C++ extension module importable as ``prefig._prefigure``.
  Build it with ``pip install -e .`` from the repo root.
* matplotlib + numpy (already pulled in by ``pip install -e .[dev]``).

See ``prefigure-cpp/docs/guide/profiling.rst`` for the full reference,
including the cold-vs-warm interpretation, the MathJax cache architecture,
and reproducible benchmark numbers.
"""

from __future__ import annotations

import argparse
import statistics
import sys
import time
from pathlib import Path
from typing import Callable, Optional

import matplotlib
import matplotlib.pyplot as plt
import numpy as np

matplotlib.rcParams.update({"font.size": 10})


# ============================================================================
# Configuration
# ============================================================================

PROJECT_ROOT = Path(__file__).parent
EXAMPLES_DIR = PROJECT_ROOT / "prefig" / "resources" / "examples"

# The 8 integration examples that ship with the library.  These are the same
# files exercised by ``verify_tests.sh`` and the C++ Catch2 ``test_golden``
# regression suite, so timings here are directly comparable with the rest of
# the test infrastructure.
EXAMPLE_FILES = [
    "tangent.xml",
    "derivatives.xml",
    "de-system.xml",
    "diffeqs.xml",
    "implicit.xml",
    "projection.xml",
    "riemann.xml",
    "roots_of_unity.xml",
]

DEFAULT_RUNS = 30
DEFAULT_WARMUP = 1


# ============================================================================
# Imports — both backends are loaded explicitly
# ============================================================================

def import_python_backend():
    """Return the Python ``core.parse.parse`` callable, or None on failure.

    We deliberately import ``prefig.core.parse`` directly instead of routing
    through ``prefig.engine.build()`` because the engine auto-selects the C++
    backend whenever the extension module is importable.  Calling the core
    module directly guarantees we're timing the pure-Python pipeline.
    """
    try:
        from prefig.core import parse as core_parse
        return core_parse.parse
    except ImportError as exc:
        print(f"[!!] Failed to import prefig.core.parse: {exc}")
        return None


def import_cpp_backend():
    """Return the C++ ``_prefigure`` module, or None if it isn't built.

    The editable install drops the compiled extension at
    ``<venv>/site-packages/prefig/_prefigure.<tag>.so`` (or directly into
    ``prefig/`` for an in-source editable layout).  Either way it's
    accessible as ``prefig._prefigure``.  The old import path
    (``import _prefigure`` from ``prefigure-cpp/build``) is no longer used.
    """
    try:
        from prefig import _prefigure
        return _prefigure
    except ImportError as exc:
        print(f"[--] C++ backend not available: {exc}")
        print("     Build with:  .venv/bin/pip install -e .")
        return None


# ============================================================================
# Timing helper
# ============================================================================

def time_fn(fn: Callable[[], None], *, runs: int, warmup: int) -> list[float]:
    """Execute ``fn`` ``warmup + runs`` times and return the last ``runs``
    durations in milliseconds.

    A small warm-up phase discards the first few timings so JIT/I/O caches
    (PyCairo font cache, MathJax Node startup, filesystem readahead, etc.)
    are primed before we start collecting samples.
    """
    for _ in range(warmup):
        fn()
    times: list[float] = []
    for _ in range(runs):
        start = time.perf_counter()
        fn()
        elapsed_ms = (time.perf_counter() - start) * 1000.0
        times.append(elapsed_ms)
    return times


# ============================================================================
# Per-backend builders
# ============================================================================

def make_python_runner(py_parse: Callable, xml_path: str) -> Callable[[], None]:
    """Return a zero-arg closure that runs one full Python build of ``xml_path``.

    Calls ``core.parse.parse`` with the same argument tuple that
    ``prefig.engine.build`` would assemble for ``--ignore-publication``.
    """
    def run():
        py_parse(
            xml_path,
            "svg",       # format
            None,        # publication file (skip discovery)
            False,       # suppress_caption
            "pf_cli",    # environment
        )
    return run


def make_cpp_runner(cpp_mod, xml_path: str) -> Callable[[], None]:
    """Return a zero-arg closure that runs one full C++ build of ``xml_path``."""
    fmt = cpp_mod.OutputFormat.SVG
    env = cpp_mod.Environment.PfCli

    def run():
        cpp_mod.parse(
            xml_path,    # filename
            fmt,         # format
            "",          # pub_file (empty = none)
            False,       # suppress_caption
            env,         # environment
        )
    return run


# ============================================================================
# Benchmark driver
# ============================================================================

def _measure_cold(runner: Callable[[], None]) -> float:
    """Run ``runner`` once with no warmup and return the elapsed milliseconds.

    The very first ``parse()`` call after process startup pays the full
    MathJax startup cost (~700 ms), since the Node.js subprocess has to
    spawn, V8 has to boot, the MathJax library has to load, and every label
    has to be typeset from scratch into the LaTeX→SVG cache.  Subsequent
    calls hit the cache and are 50–100× faster.  This function exists so
    we can report that one-time cold cost separately from the warm-cache
    averages that everyone actually cares about.
    """
    start = time.perf_counter()
    runner()
    return (time.perf_counter() - start) * 1000.0


def run_benchmarks(py_parse, cpp_mod, *, runs: int, warmup: int,
                   report_cold: bool = False,
                   only: Optional[list[str]] = None):
    """Run the integration tests against both backends.

    Returns a list of result dicts, one per example, with keys:
        name, py_times, py_mean, py_std, cpp_times, cpp_mean, cpp_std
        (and py_cold_ms / cpp_cold_ms when ``report_cold`` is set)
    Missing backends produce None entries (not zeros) so the plotter can
    distinguish a missing measurement from a real zero.

    If ``only`` is supplied, only those diagram names (with or without the
    .xml suffix, case-insensitive) are timed.  Unknown names raise
    RuntimeError so a typo on the command line doesn't silently produce
    an empty benchmark.
    """
    results = []
    available = [f for f in EXAMPLE_FILES if (EXAMPLES_DIR / f).exists()]
    if not available:
        raise RuntimeError(f"No example files found under {EXAMPLES_DIR}")

    if only:
        # Normalise: drop ".xml", lowercase
        wanted = {n.lower().removesuffix(".xml") for n in only}
        known = {f.lower().removesuffix(".xml"): f for f in available}
        unknown = wanted - known.keys()
        if unknown:
            raise RuntimeError(
                "Unknown diagram name(s): " + ", ".join(sorted(unknown)) +
                "\nAvailable: " + ", ".join(sorted(known.keys())))
        available = [known[n] for n in sorted(wanted)]

    if warmup < 1:
        print(
            "WARNING: --warmup is < 1.  The first timed run for each diagram "
            "will pay the one-time MathJax startup cost (~700 ms).  Pass "
            "--warmup 1 (or higher) so the LaTeX→SVG cache is populated "
            "before the timed runs begin.\n"
        )

    width = max(len(name) for name in available)

    for idx, fname in enumerate(available, start=1):
        xml_path = str(EXAMPLES_DIR / fname)
        label = fname.replace(".xml", "")

        py_cold_ms: Optional[float] = None
        cpp_cold_ms: Optional[float] = None
        py_times: Optional[list[float]] = None
        cpp_times: Optional[list[float]] = None

        print(f"[{idx}/{len(available)}] {fname:<{width}} ", end="", flush=True)

        if py_parse is not None:
            try:
                runner = make_python_runner(py_parse, xml_path)
                if report_cold:
                    # Measure the very first call before any warmup, while
                    # the LaTeX→SVG cache is still cold for this diagram.
                    py_cold_ms = _measure_cold(runner)
                py_times = time_fn(runner, runs=runs, warmup=warmup)
            except Exception as exc:
                print(f"\n     [!] Python backend failed: {exc}")
                py_times = None

        if cpp_mod is not None:
            try:
                runner = make_cpp_runner(cpp_mod, xml_path)
                if report_cold:
                    cpp_cold_ms = _measure_cold(runner)
                cpp_times = time_fn(runner, runs=runs, warmup=warmup)
            except Exception as exc:
                print(f"\n     [!] C++ backend failed: {exc}")
                cpp_times = None

        py_mean = statistics.mean(py_times) if py_times else None
        py_std = statistics.stdev(py_times) if py_times and len(py_times) > 1 else 0.0
        cpp_mean = statistics.mean(cpp_times) if cpp_times else None
        cpp_std = statistics.stdev(cpp_times) if cpp_times and len(cpp_times) > 1 else 0.0

        # Single-line summary
        parts = []
        if py_mean is not None:
            parts.append(f"Py={py_mean:7.1f}ms (±{py_std:5.1f})")
        if cpp_mean is not None:
            parts.append(f"C++={cpp_mean:7.1f}ms (±{cpp_std:5.1f})")
        if py_mean and cpp_mean and cpp_mean > 0:
            parts.append(f"speedup {py_mean / cpp_mean:5.2f}x")
        print("  ".join(parts))

        results.append({
            "name": label,
            "py_times": py_times,
            "py_mean": py_mean,
            "py_std": py_std,
            "py_cold_ms": py_cold_ms,
            "cpp_times": cpp_times,
            "cpp_mean": cpp_mean,
            "cpp_std": cpp_std,
            "cpp_cold_ms": cpp_cold_ms,
        })

    return results


# ============================================================================
# Plotting
# ============================================================================

def plot_results(results: list[dict], runs: int, output_file: Optional[str],
                 show: bool = False) -> None:
    """Render a single grouped bar chart with one (Py, C++) pair per example.

    The Python and C++ averages are placed side by side for each diagram, with
    error bars showing one standard deviation.  Bars are annotated with the
    average runtime, and a per-bar speedup factor is shown above each pair
    when both backends were measured.
    """
    if not results:
        print("No results to plot.")
        return

    names = [r["name"] for r in results]
    py_means = [r["py_mean"] or 0 for r in results]
    py_stds = [r["py_std"] or 0 for r in results]
    cpp_means = [r["cpp_mean"] or 0 for r in results]
    cpp_stds = [r["cpp_std"] or 0 for r in results]

    have_py = any(r["py_mean"] is not None for r in results)
    have_cpp = any(r["cpp_mean"] is not None for r in results)

    x = np.arange(len(names))
    bar_width = 0.38

    fig, ax = plt.subplots(figsize=(max(11, 1.4 * len(names) + 4), 7))

    PY_COLOR = "#4C72B0"
    CPP_COLOR = "#DD8452"

    py_bars = None
    cpp_bars = None

    if have_py:
        py_bars = ax.bar(
            x - bar_width / 2 if have_cpp else x,
            py_means,
            bar_width if have_cpp else bar_width * 1.6,
            yerr=py_stds,
            color=PY_COLOR,
            edgecolor="black",
            linewidth=0.5,
            capsize=4,
            label="Python",
        )

    if have_cpp:
        cpp_bars = ax.bar(
            x + bar_width / 2 if have_py else x,
            cpp_means,
            bar_width if have_py else bar_width * 1.6,
            yerr=cpp_stds,
            color=CPP_COLOR,
            edgecolor="black",
            linewidth=0.5,
            capsize=4,
            label="C++",
        )

    # Per-bar value labels
    def annotate(bars, means, stds):
        for bar, mean, std in zip(bars, means, stds):
            if mean <= 0:
                continue
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                bar.get_height() + std + max(py_means + cpp_means) * 0.015,
                f"{mean:.0f} ms",
                ha="center", va="bottom", fontsize=8,
            )

    if py_bars is not None:
        annotate(py_bars, py_means, py_stds)
    if cpp_bars is not None:
        annotate(cpp_bars, cpp_means, cpp_stds)

    # Speedup annotations above each pair
    if have_py and have_cpp:
        max_h = max(
            (py_means[i] + py_stds[i]) for i in range(len(names))
        ) if py_means else 0
        max_h = max(max_h, max(
            (cpp_means[i] + cpp_stds[i]) for i in range(len(names))
        ) if cpp_means else 0)
        for i, r in enumerate(results):
            if r["py_mean"] and r["cpp_mean"] and r["cpp_mean"] > 0:
                speedup = r["py_mean"] / r["cpp_mean"]
                color = "green" if speedup > 1 else "red"
                ax.text(
                    x[i],
                    max_h * 1.08,
                    f"{speedup:.1f}×",
                    ha="center", va="bottom",
                    fontsize=11, fontweight="bold", color=color,
                    bbox=dict(boxstyle="round,pad=0.25", facecolor="white",
                              edgecolor=color, alpha=0.85),
                )
        # Make headroom for the speedup boxes
        ax.set_ylim(top=max_h * 1.25)

    ax.set_xticks(x)
    ax.set_xticklabels(names, rotation=20, ha="right")
    ax.set_ylabel("Average build time (ms)")
    ax.set_title(
        "PreFigure end-to-end build time: Python vs C++  "
        f"(warm cache, n={runs} runs per diagram, mean ± stdev)",
        fontsize=12, fontweight="bold",
    )
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    ax.legend(loc="upper left")

    fig.tight_layout()

    # Always save the plot to disk so the script never blocks waiting for an
    # interactive matplotlib window to be dismissed.  When the user passes
    # --output, save there.  Otherwise, save to a fresh path under the temp
    # directory and print it.  Only call plt.show() (which is blocking) when
    # the user explicitly asks for it via --show.
    if not output_file:
        import tempfile
        from datetime import datetime
        ts = datetime.now().strftime("%Y%m%d-%H%M%S")
        output_file = str(Path(tempfile.gettempdir()) / f"prefigure-profile-{ts}.png")
    fig.savefig(output_file, dpi=150, bbox_inches="tight")
    print(f"\nPlot saved to {output_file}")

    if show:
        plt.show()


# ============================================================================
# Summary table
# ============================================================================

def print_summary(results: list[dict], runs: int) -> None:
    """Print a tidy summary table after all benchmarks have run.

    The "warm" columns are the average over ``runs`` post-warmup samples,
    where the LaTeX→SVG cache is fully populated.  When the benchmark was
    invoked with ``--report-cold``, two extra columns show the wall-clock
    cost of the very first call (which includes the one-time MathJax
    startup of ~700 ms).
    """
    has_cold = any(r.get("py_cold_ms") is not None or r.get("cpp_cold_ms") is not None
                   for r in results)

    print()
    width = 110 if has_cold else 78
    print("=" * width)
    print(f"  Summary  (warm cache, n = {runs} runs per diagram, times in milliseconds)")
    print("=" * width)

    if has_cold:
        print(f"  {'diagram':<22} {'Python μ':>10} {'Python σ':>9} "
              f"{'cold Py':>9}  {'C++ μ':>10} {'C++ σ':>9} {'cold C++':>10}  {'speedup':>9}")
    else:
        print(f"  {'diagram':<22} {'Python μ':>12} {'Python σ':>10} "
              f"{'C++ μ':>12} {'C++ σ':>10}  {'speedup':>9}")
    print("  " + "-" * (width - 2))

    for r in results:
        if r["py_mean"] and r["cpp_mean"] and r["cpp_mean"] > 0:
            sp = f"{r['py_mean'] / r['cpp_mean']:>8.2f}×"
        else:
            sp = f"{'—':>9}"

        if has_cold:
            py = f"{r['py_mean']:>10.1f}" if r["py_mean"] is not None else f"{'—':>10}"
            pys = f"{r['py_std']:>9.1f}" if r["py_mean"] is not None else f"{'—':>9}"
            pyc = (f"{r['py_cold_ms']:>9.1f}"
                   if r.get("py_cold_ms") is not None else f"{'—':>9}")
            cp = f"{r['cpp_mean']:>10.1f}" if r["cpp_mean"] is not None else f"{'—':>10}"
            cps = f"{r['cpp_std']:>9.1f}" if r["cpp_mean"] is not None else f"{'—':>9}"
            cpc = (f"{r['cpp_cold_ms']:>10.1f}"
                   if r.get("cpp_cold_ms") is not None else f"{'—':>10}")
            print(f"  {r['name']:<22} {py} {pys} {pyc}  {cp} {cps} {cpc}  {sp}")
        else:
            py = f"{r['py_mean']:>12.1f}" if r["py_mean"] is not None else f"{'—':>12}"
            pys = f"{r['py_std']:>10.1f}" if r["py_mean"] is not None else f"{'—':>10}"
            cp = f"{r['cpp_mean']:>12.1f}" if r["cpp_mean"] is not None else f"{'—':>12}"
            cps = f"{r['cpp_std']:>10.1f}" if r["cpp_mean"] is not None else f"{'—':>10}"
            print(f"  {r['name']:<22} {py} {pys} {cp} {cps}  {sp}")

    print("=" * width)


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="PreFigure end-to-end Python vs C++ benchmark "
                    "(8 integration tests, 30 samples each by default).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--runs", type=int, default=DEFAULT_RUNS,
        help="Number of timed runs per diagram per backend.",
    )
    parser.add_argument(
        "--warmup", type=int, default=DEFAULT_WARMUP,
        help="Discard the first N runs of each backend before measuring.",
    )
    parser.add_argument(
        "--output", type=str, default=None,
        help=(
            "Save the plot to this PNG path.  When omitted, the plot is "
            "auto-saved to /tmp/prefigure-profile-<timestamp>.png and the "
            "path is printed at the end of the run.  Pass --show to also "
            "open it in an interactive matplotlib window."
        ),
    )
    parser.add_argument(
        "--no-cpp", action="store_true",
        help="Skip the C++ backend (Python-only profiling).",
    )
    parser.add_argument(
        "--no-python", action="store_true",
        help="Skip the Python backend (C++-only profiling).",
    )
    parser.add_argument(
        "--report-cold", action="store_true",
        help=(
            "Also measure and report the cost of the very first call per "
            "diagram, before any warmup runs.  The first call pays the one-"
            "time MathJax startup cost (~700 ms) that subsequent cached "
            "calls avoid.  Adds two columns ('cold Py' / 'cold C++') to the "
            "summary table; the bar chart still shows warm-cache averages."
        ),
    )
    parser.add_argument(
        "--diagram", "-d", action="append", default=None, metavar="NAME",
        help=(
            "Restrict the benchmark to one or more diagrams.  NAME may be "
            "given with or without the '.xml' suffix; matching is "
            "case-insensitive.  Pass the flag multiple times to select "
            "several diagrams (e.g. '--diagram tangent --diagram diffeqs'). "
            "Without this flag the script runs all 8 example diagrams."
        ),
    )
    parser.add_argument(
        "--list", action="store_true",
        help="List the available example diagrams and exit.",
    )
    parser.add_argument(
        "--show", action="store_true",
        help=(
            "Open an interactive matplotlib window after saving the plot. "
            "This call is BLOCKING — the script waits for you to close the "
            "window before exiting.  Without this flag the script always "
            "saves the plot to a file and returns immediately."
        ),
    )
    args = parser.parse_args()

    if args.list:
        print("Available example diagrams (in " + str(EXAMPLES_DIR) + "):")
        for f in EXAMPLE_FILES:
            mark = " " if (EXAMPLES_DIR / f).exists() else "?"
            print(f"  {mark} {f.replace('.xml', '')}")
        sys.exit(0)

    if args.no_cpp and args.no_python:
        parser.error("Cannot pass both --no-cpp and --no-python.")

    print("=" * 78)
    print("  PreFigure end-to-end performance comparison")
    print("=" * 78)
    print(f"  examples dir : {EXAMPLES_DIR}")
    print(f"  runs/diagram : {args.runs}  (warmup: {args.warmup})")
    print()

    py_parse = None if args.no_python else import_python_backend()
    if py_parse is not None:
        print("[OK] Python backend (prefig.core.parse) loaded")
    elif not args.no_python:
        sys.exit("ERROR: Python backend unavailable; cannot continue.")

    cpp_mod = None if args.no_cpp else import_cpp_backend()
    if cpp_mod is not None:
        print("[OK] C++ backend (prefig._prefigure) loaded")
    elif not args.no_cpp:
        print("     Continuing with Python-only timings.")
    print()

    if py_parse is None and cpp_mod is None:
        sys.exit("ERROR: No backends available.")

    try:
        results = run_benchmarks(
            py_parse, cpp_mod,
            runs=args.runs,
            warmup=args.warmup,
            report_cold=args.report_cold,
            only=args.diagram,
        )
    except RuntimeError as exc:
        # Surface --diagram typos and similar setup errors as a clean
        # one-line message instead of an internal stack trace.
        sys.exit(f"ERROR: {exc}")
    print_summary(results, runs=args.runs)
    plot_results(results, runs=args.runs, output_file=args.output, show=args.show)
    print("Done.")


if __name__ == "__main__":
    main()
