#!/usr/bin/env python3
"""
PreFigure: Python vs C++ end-to-end performance comparison.

For each of the 8 example XML diagrams, this script runs the full build
pipeline 30 times against both backends, computes per-diagram averages,
and renders a side-by-side bar chart of the results.

The two backends are dispatched directly (no auto-selection):

    Python:  prefig.core.parse.parse(filename, "svg", None, False, "pf_cli")
    C++:     _prefigure.parse(filename, _prefigure.OutputFormat.SVG, "",
                              False, _prefigure.Environment.PfCli)

This bypasses ``prefig.engine.build()`` so the Python timing isn't
contaminated by the engine's automatic backend autodetection (which
silently switches to C++ whenever the extension is importable).

USAGE
-----
From the repository root, with the editable install built:

    .venv/bin/python profiling_comparison.py
    .venv/bin/python profiling_comparison.py --runs 30 --output prof.png
    .venv/bin/python profiling_comparison.py --no-cpp     # Python only
    .venv/bin/python profiling_comparison.py --warmup 2   # discard first N

REQUIREMENTS
------------
* The Python ``prefig`` package importable (``pip install -e .``).
* The C++ extension module importable as ``prefig._prefigure``.
  Build it with ``pip install -e .`` from the repo root.
* matplotlib + numpy (already pulled in by ``pip install -e .[dev]``).
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

def run_benchmarks(py_parse, cpp_mod, *, runs: int, warmup: int):
    """Run all 8 integration tests against both backends.

    Returns a list of result dicts, one per example, with keys:
        name, py_times, py_mean, py_std, cpp_times, cpp_mean, cpp_std
    Missing backends produce None entries (not zeros) so the plotter can
    distinguish a missing measurement from a real zero.
    """
    results = []
    available = [f for f in EXAMPLE_FILES if (EXAMPLES_DIR / f).exists()]
    if not available:
        raise RuntimeError(f"No example files found under {EXAMPLES_DIR}")

    width = max(len(name) for name in available)

    for idx, fname in enumerate(available, start=1):
        xml_path = str(EXAMPLES_DIR / fname)
        label = fname.replace(".xml", "")

        py_times: Optional[list[float]] = None
        cpp_times: Optional[list[float]] = None

        print(f"[{idx}/{len(available)}] {fname:<{width}} ", end="", flush=True)

        if py_parse is not None:
            try:
                runner = make_python_runner(py_parse, xml_path)
                py_times = time_fn(runner, runs=runs, warmup=warmup)
            except Exception as exc:
                print(f"\n     [!] Python backend failed: {exc}")
                py_times = None

        if cpp_mod is not None:
            try:
                runner = make_cpp_runner(cpp_mod, xml_path)
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
            "cpp_times": cpp_times,
            "cpp_mean": cpp_mean,
            "cpp_std": cpp_std,
        })

    return results


# ============================================================================
# Plotting
# ============================================================================

def plot_results(results: list[dict], runs: int, output_file: Optional[str]) -> None:
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
        f"PreFigure end-to-end build time: Python vs C++  "
        f"(n={runs} runs per diagram, mean ± stdev)",
        fontsize=12, fontweight="bold",
    )
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    ax.legend(loc="upper left")

    fig.tight_layout()

    if output_file:
        fig.savefig(output_file, dpi=150, bbox_inches="tight")
        print(f"\nPlot saved to {output_file}")
    else:
        plt.show()


# ============================================================================
# Summary table
# ============================================================================

def print_summary(results: list[dict], runs: int) -> None:
    """Print a tidy summary table after all benchmarks have run."""
    print()
    print("=" * 78)
    print(f"  Summary  (n = {runs} runs per diagram, times in milliseconds)")
    print("=" * 78)
    print(f"  {'diagram':<22} {'Python μ':>12} {'Python σ':>10} "
          f"{'C++ μ':>12} {'C++ σ':>10}  {'speedup':>9}")
    print("  " + "-" * 76)
    for r in results:
        py = f"{r['py_mean']:>12.1f}" if r["py_mean"] is not None else f"{'—':>12}"
        pys = f"{r['py_std']:>10.1f}" if r["py_mean"] is not None else f"{'—':>10}"
        cp = f"{r['cpp_mean']:>12.1f}" if r["cpp_mean"] is not None else f"{'—':>12}"
        cps = f"{r['cpp_std']:>10.1f}" if r["cpp_mean"] is not None else f"{'—':>10}"
        if r["py_mean"] and r["cpp_mean"] and r["cpp_mean"] > 0:
            sp = f"{r['py_mean'] / r['cpp_mean']:>8.2f}×"
        else:
            sp = f"{'—':>9}"
        print(f"  {r['name']:<22} {py} {pys} {cp} {cps}  {sp}")
    print("=" * 78)


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
        help="Save the plot to this PNG path (otherwise opens an interactive window).",
    )
    parser.add_argument(
        "--no-cpp", action="store_true",
        help="Skip the C++ backend (Python-only profiling).",
    )
    parser.add_argument(
        "--no-python", action="store_true",
        help="Skip the Python backend (C++-only profiling).",
    )
    args = parser.parse_args()

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

    results = run_benchmarks(py_parse, cpp_mod, runs=args.runs, warmup=args.warmup)
    print_summary(results, runs=args.runs)
    plot_results(results, runs=args.runs, output_file=args.output)
    print("Done.")


if __name__ == "__main__":
    main()
