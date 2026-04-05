#!/usr/bin/env python3
"""
PreFigure: Python vs C++ Performance Comparison

This driver script benchmarks the Python and C++ implementations of the
PreFigure library side by side, running identical test cases through both
backends and plotting the results.

Usage:
    python profiling_comparison.py [--runs N] [--output FILE]

The C++ backend must be built first:
    cd prefigure-cpp && ./build.sh --release

Then ensure the _prefigure shared library is importable:
    export PYTHONPATH=prefigure-cpp/build:$PYTHONPATH
"""

import argparse
import importlib
import math
import os
import statistics
import sys
import time
from pathlib import Path

import matplotlib.pyplot as plt
import matplotlib
import numpy as np

matplotlib.rcParams.update({"font.size": 10})


# ============================================================================
# Configuration
# ============================================================================

PROJECT_ROOT = Path(__file__).parent
EXAMPLES_DIR = PROJECT_ROOT / "prefig" / "resources" / "examples"
CPP_BUILD_DIR = PROJECT_ROOT / "prefigure-cpp" / "build"

# Example XML files to benchmark for full diagram builds
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

DEFAULT_RUNS = 5


# ============================================================================
# Helpers
# ============================================================================

def time_fn(fn, *args, runs=5, **kwargs):
    """Time a function over multiple runs, return list of durations in ms."""
    times = []
    for _ in range(runs):
        start = time.perf_counter()
        fn(*args, **kwargs)
        elapsed = (time.perf_counter() - start) * 1000.0  # ms
        times.append(elapsed)
    return times


def try_import_cpp():
    """Try to import the C++ pybind11 module."""
    # Add build directory to path if needed
    build_lib = CPP_BUILD_DIR
    if build_lib.exists() and str(build_lib) not in sys.path:
        sys.path.insert(0, str(build_lib))

    try:
        import _prefigure as cpp_mod
        return cpp_mod
    except ImportError:
        return None


def try_import_python():
    """Import the Python prefigure library."""
    try:
        from prefig.core import user_namespace as un
        from prefig.core import calculus
        from prefig.core import math_utilities as mu
        from prefig.core import CTM
        from prefig import engine
        return {
            "user_namespace": un,
            "calculus": calculus,
            "math_utilities": mu,
            "CTM": CTM,
            "engine": engine,
        }
    except ImportError as e:
        print(f"Warning: Could not import Python prefigure: {e}")
        return None


# ============================================================================
# Benchmark definitions
#
# Each benchmark is a dict with:
#   name:     Display name
#   python:   callable(runs) -> list of times in ms
#   cpp:      callable(runs, cpp_mod) -> list of times in ms
# ============================================================================

def make_benchmarks(py_mods, cpp_mod):
    """Create benchmark suite based on available modules."""
    benchmarks = []

    # ------------------------------------------------------------------
    # 1. Numerical derivative via Richardson extrapolation
    # ------------------------------------------------------------------
    if py_mods:
        calc = py_mods["calculus"]

        def py_derivative(runs):
            def work():
                for _ in range(1000):
                    calc.derivative(math.sin, 1.0)
                    calc.derivative(math.exp, 2.0)
                    calc.derivative(lambda x: x**3 + 2*x, 3.0)
            return time_fn(work, runs=runs)

        benchmarks.append({
            "name": "Numerical Derivative\n(1000 Richardson extrapolations)",
            "python": py_derivative,
            "cpp": None,
        })

    # ------------------------------------------------------------------
    # 2. Expression evaluation (define + evaluate functions)
    # ------------------------------------------------------------------
    if py_mods:
        un = py_mods["user_namespace"]

        def py_expr_eval(runs):
            def work():
                importlib.reload(un)
                for _ in range(500):
                    un.valid_eval("3 + 4 * 2")
                    un.valid_eval("sin(pi/4)")
                    un.define("f(x) = x^2 + 3*x + 1")
                    un.valid_eval("f(5)")
                    un.valid_eval("(1, 2, 3)")
            return time_fn(work, runs=runs)

        benchmarks.append({
            "name": "Expression Evaluation\n(500 parse/eval cycles)",
            "python": py_expr_eval,
            "cpp": None,
        })

    # ------------------------------------------------------------------
    # 3. CTM coordinate transforms
    # ------------------------------------------------------------------
    if py_mods:
        ctm_mod = py_mods["CTM"]

        def py_ctm_transforms(runs):
            def work():
                for _ in range(2000):
                    ctm = ctm_mod.CTM()
                    ctm.translate(10, 20)
                    ctm.scale(2, 3)
                    ctm.rotate(45)
                    ctm.transform(np.array([5.0, 7.0]))
                    ctm.inverse_transform(np.array([30.0, 81.0]))
            return time_fn(work, runs=runs)

        benchmarks.append({
            "name": "CTM Transforms\n(2000 transform chains)",
            "python": py_ctm_transforms,
            "cpp": None,
        })

    # ------------------------------------------------------------------
    # 4. Vector math operations
    # ------------------------------------------------------------------
    if py_mods:
        mu = py_mods["math_utilities"]

        def py_vector_math(runs):
            def work():
                u = np.array([3.0, 4.0])
                v = np.array([1.0, 2.0])
                for _ in range(5000):
                    mu.dot(u, v)
                    mu.length(u)
                    mu.normalize(u)
                    mu.midpoint(u, v)
                    mu.distance(u, v)
                    mu.angle(u)
                    mu.rotate(u, 0.5)
            return time_fn(work, runs=runs)

        benchmarks.append({
            "name": "Vector Math\n(5000 operation batches)",
            "python": py_vector_math,
            "cpp": None,
        })

    # ------------------------------------------------------------------
    # 5. Bezier curve evaluation
    # ------------------------------------------------------------------
    if py_mods:
        mu = py_mods["math_utilities"]

        def py_bezier(runs):
            controls = [np.array([0.0, 0.0]), np.array([1.0, 2.0]),
                        np.array([3.0, 1.0]), np.array([4.0, 0.0])]
            def work():
                for _ in range(5000):
                    for t in [0.0, 0.25, 0.5, 0.75, 1.0]:
                        mu.evaluate_bezier(controls, t)
            return time_fn(work, runs=runs)

        benchmarks.append({
            "name": "Bezier Evaluation\n(25000 cubic evaluations)",
            "python": py_bezier,
            "cpp": None,
        })

    # ------------------------------------------------------------------
    # 6. Root finding / intersection
    # ------------------------------------------------------------------
    if py_mods:
        mu = py_mods["math_utilities"]

        def py_intersect(runs):
            mu.set_diagram(type("FakeDiagram", (), {
                "bbox": lambda self: [0, 0, 10, 10]
            })())
            def work():
                for _ in range(200):
                    f = lambda x: x**2 - 4
                    mu.intersect(f, seed=1.0, interval=(0, 10))
            return time_fn(work, runs=runs)

        benchmarks.append({
            "name": "Root Finding\n(200 bisection searches)",
            "python": py_intersect,
            "cpp": None,
        })

    # ------------------------------------------------------------------
    # 7. Full diagram build (if examples exist)
    # ------------------------------------------------------------------
    if py_mods and EXAMPLES_DIR.exists():
        engine = py_mods["engine"]
        available_examples = [
            f for f in EXAMPLE_FILES
            if (EXAMPLES_DIR / f).exists()
        ]

        for example_file in available_examples:
            example_path = str(EXAMPLES_DIR / example_file)
            example_name = example_file.replace(".xml", "")

            def make_py_build(path):
                def py_build(runs):
                    def work():
                        engine.build(
                            "svg",
                            path,
                            ignore_publication=True,
                            environment="pf_cli",
                        )
                    return time_fn(work, runs=runs)
                return py_build

            benchmarks.append({
                "name": f"Full Build: {example_name}",
                "python": make_py_build(example_path),
                "cpp": None,
            })

    # ------------------------------------------------------------------
    # Wire up C++ benchmarks where C++ module is available
    # ------------------------------------------------------------------
    if cpp_mod is not None:
        # C++ module exposes: derivative, CTM operations, expression eval, etc.
        # via the pybind11 bindings

        for bench in benchmarks:
            name = bench["name"]

            if "Derivative" in name and hasattr(cpp_mod, "derivative"):
                def cpp_derivative(runs, mod=cpp_mod):
                    def work():
                        for _ in range(1000):
                            mod.derivative(math.sin, 1.0)
                            mod.derivative(math.exp, 2.0)
                            mod.derivative(lambda x: x**3 + 2*x, 3.0)
                    return time_fn(work, runs=runs)
                bench["cpp"] = cpp_derivative

            elif "Expression" in name and hasattr(cpp_mod, "ExpressionContext"):
                def cpp_expr_eval(runs, mod=cpp_mod):
                    def work():
                        ctx = mod.ExpressionContext()
                        for _ in range(500):
                            ctx.eval("3 + 4 * 2")
                            ctx.eval("sin(pi/4)")
                            ctx.define("f(x) = x^2 + 3*x + 1")
                            ctx.eval("f(5)")
                            ctx.eval("(1, 2, 3)")
                    return time_fn(work, runs=runs)
                bench["cpp"] = cpp_expr_eval

            elif "CTM" in name and hasattr(cpp_mod, "CTM"):
                def cpp_ctm(runs, mod=cpp_mod):
                    def work():
                        for _ in range(2000):
                            ctm = mod.CTM()
                            ctm.translate(10, 20)
                            ctm.scale(2, 3)
                            ctm.rotate(45)
                            ctm.transform((5.0, 7.0))
                            ctm.inverse_transform((30.0, 81.0))
                    return time_fn(work, runs=runs)
                bench["cpp"] = cpp_ctm

            elif "Full Build" in name and hasattr(cpp_mod, "parse"):
                example_name = name.split(": ")[1]
                example_path = str(EXAMPLES_DIR / f"{example_name}.xml")

                def make_cpp_build(path, mod=cpp_mod):
                    def cpp_build(runs):
                        def work():
                            mod.parse(
                                path, "",
                                None,
                                mod.OutputFormat.SVG,
                                None,
                                mod.Environment.PfCli,
                            )
                        return time_fn(work, runs=runs)
                    return cpp_build

                bench["cpp"] = make_cpp_build(example_path)

    return benchmarks


# ============================================================================
# Plotting
# ============================================================================

def plot_results(results, output_file=None):
    """
    Plot benchmark results as a grid of bar charts.

    results: list of dicts with keys:
        name, python_mean, python_std, cpp_mean, cpp_std
    """
    n = len(results)
    if n == 0:
        print("No benchmark results to plot.")
        return

    # Determine grid layout
    ncols = min(3, n)
    nrows = math.ceil(n / ncols)
    fig, axes = plt.subplots(nrows, ncols, figsize=(5.5 * ncols, 4.5 * nrows))

    if n == 1:
        axes = np.array([axes])
    axes = np.atleast_2d(axes)

    bar_width = 0.35
    colors_py = "#4C72B0"
    colors_cpp = "#DD8452"

    for idx, result in enumerate(results):
        row, col = divmod(idx, ncols)
        ax = axes[row][col]

        labels = []
        means = []
        stds = []
        colors = []

        if result["python_mean"] is not None:
            labels.append("Python")
            means.append(result["python_mean"])
            stds.append(result["python_std"])
            colors.append(colors_py)

        if result["cpp_mean"] is not None:
            labels.append("C++")
            means.append(result["cpp_mean"])
            stds.append(result["cpp_std"])
            colors.append(colors_cpp)

        x = np.arange(len(labels))
        bars = ax.bar(x, means, bar_width * 2, yerr=stds,
                      color=colors, capsize=5, edgecolor="black", linewidth=0.5)

        ax.set_xticks(x)
        ax.set_xticklabels(labels, fontweight="bold")
        ax.set_ylabel("Time (ms)")
        ax.set_title(result["name"], fontsize=10)

        # Add value labels on bars
        for bar, mean, std in zip(bars, means, stds):
            ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + std + 0.5,
                    f"{mean:.2f} ms",
                    ha="center", va="bottom", fontsize=8)

        # Add speedup annotation if both bars present
        if result["python_mean"] is not None and result["cpp_mean"] is not None:
            if result["cpp_mean"] > 0:
                speedup = result["python_mean"] / result["cpp_mean"]
                ax.text(0.95, 0.95, f"{speedup:.1f}x",
                        transform=ax.transAxes, ha="right", va="top",
                        fontsize=14, fontweight="bold",
                        color="green" if speedup > 1 else "red",
                        bbox=dict(boxstyle="round,pad=0.3", facecolor="white",
                                  edgecolor="gray", alpha=0.8))

        ax.grid(axis="y", alpha=0.3)
        ax.set_axisbelow(True)

    # Hide unused subplots
    for idx in range(n, nrows * ncols):
        row, col = divmod(idx, ncols)
        axes[row][col].set_visible(False)

    fig.suptitle("PreFigure: Python vs C++ Performance Comparison",
                 fontsize=14, fontweight="bold", y=1.02)
    fig.tight_layout()

    if output_file:
        fig.savefig(output_file, dpi=150, bbox_inches="tight")
        print(f"Plot saved to {output_file}")
    else:
        plt.show()


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="PreFigure Python vs C++ performance comparison"
    )
    parser.add_argument(
        "--runs", type=int, default=DEFAULT_RUNS,
        help=f"Number of timed runs per benchmark (default: {DEFAULT_RUNS})"
    )
    parser.add_argument(
        "--output", type=str, default=None,
        help="Output file for the plot (e.g., comparison.png). Shows interactive plot if omitted."
    )
    parser.add_argument(
        "--no-cpp", action="store_true",
        help="Skip C++ benchmarks (Python-only profiling)"
    )
    parser.add_argument(
        "--no-full-build", action="store_true",
        help="Skip full diagram build benchmarks (faster iteration)"
    )
    args = parser.parse_args()

    print("=" * 60)
    print("PreFigure: Python vs C++ Performance Comparison")
    print("=" * 60)
    print(f"Runs per benchmark: {args.runs}")
    print()

    # Import modules
    py_mods = try_import_python()
    if py_mods:
        print("[OK] Python prefigure library loaded")
    else:
        print("[!!] Python prefigure library not found")
        print("     Install with: pip install -e .")
        sys.exit(1)

    cpp_mod = None
    if not args.no_cpp:
        cpp_mod = try_import_cpp()
        if cpp_mod:
            print("[OK] C++ _prefigure module loaded")
        else:
            print("[--] C++ _prefigure module not available")
            print("     Build with: cd prefigure-cpp && ./build.sh --release")
            print("     Then: export PYTHONPATH=prefigure-cpp/build:$PYTHONPATH")
            print("     Running Python-only benchmarks...")
    print()

    # Build benchmark suite
    benchmarks = make_benchmarks(py_mods, cpp_mod)

    # Filter out full builds if requested
    if args.no_full_build:
        benchmarks = [b for b in benchmarks if "Full Build" not in b["name"]]

    # Run benchmarks
    results = []
    for i, bench in enumerate(benchmarks):
        name = bench["name"]
        short_name = name.replace("\n", " ")
        print(f"[{i+1}/{len(benchmarks)}] {short_name}...", end=" ", flush=True)

        py_times = bench["python"](args.runs) if bench["python"] else None
        cpp_times = bench["cpp"](args.runs, cpp_mod) if bench["cpp"] and cpp_mod else None

        result = {
            "name": name,
            "python_mean": statistics.mean(py_times) if py_times else None,
            "python_std": statistics.stdev(py_times) if py_times and len(py_times) > 1 else 0,
            "cpp_mean": statistics.mean(cpp_times) if cpp_times else None,
            "cpp_std": statistics.stdev(cpp_times) if cpp_times and len(cpp_times) > 1 else 0,
        }
        results.append(result)

        # Print summary
        parts = []
        if result["python_mean"] is not None:
            parts.append(f"Py={result['python_mean']:.2f}ms")
        if result["cpp_mean"] is not None:
            parts.append(f"C++={result['cpp_mean']:.2f}ms")
        if result["python_mean"] and result["cpp_mean"] and result["cpp_mean"] > 0:
            speedup = result["python_mean"] / result["cpp_mean"]
            parts.append(f"({speedup:.1f}x)")
        print(" | ".join(parts))

    print()
    print("=" * 60)
    print("Plotting results...")
    plot_results(results, output_file=args.output)
    print("Done.")


if __name__ == "__main__":
    main()
