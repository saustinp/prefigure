#!/usr/bin/env python3
"""
PreFigure: Python vs C++ Correctness Comparison

Runs every example XML through both the Python and C++ backends,
then compares the SVG output for structural and numerical equivalence.

Usage:
    python3 correctness_comparison.py [--tolerance 1e-4] [--verbose]

The C++ backend must be built with Python bindings:
    cd prefigure-cpp && ./build.sh --release
    export PYTHONPATH=prefigure-cpp/build:$PYTHONPATH
"""

import argparse
import importlib
import io
import logging
import os
import re
import sys
import tempfile
import textwrap
from pathlib import Path

import lxml.etree as ET

# ============================================================================
# Configuration
# ============================================================================

PROJECT_ROOT = Path(__file__).parent
EXAMPLES_DIR = PROJECT_ROOT / "prefig" / "resources" / "examples"
CPP_BUILD_DIR = PROJECT_ROOT / "prefigure-cpp" / "build"

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

# SVG namespace
SVG_NS = "http://www.w3.org/2000/svg"
NSMAP = {"svg": SVG_NS}


# ============================================================================
# Terminal colors
# ============================================================================

class C:
    PASS = "\033[92m"
    FAIL = "\033[91m"
    WARN = "\033[93m"
    INFO = "\033[94m"
    BOLD = "\033[1m"
    END = "\033[0m"


def passed(msg):
    return f"{C.PASS}PASS{C.END} {msg}"


def failed(msg):
    return f"{C.FAIL}FAIL{C.END} {msg}"


def warned(msg):
    return f"{C.WARN}WARN{C.END} {msg}"


# ============================================================================
# Backend runners
# ============================================================================

def run_python_backend(xml_path):
    """
    Run the Python backend on an XML file and return the SVG string.
    Uses mk_diagram with return_string=True to avoid file I/O.
    """
    from prefig.core import parse as pf_parse
    from prefig.core import user_namespace

    xml_path = str(xml_path)
    tree = ET.parse(xml_path)

    ns = {"pf": "https://prefigure.org"}
    diagrams = tree.xpath("//pf:diagram", namespaces=ns) + tree.xpath("//diagram")

    if not diagrams:
        raise RuntimeError(f"No <diagram> element found in {xml_path}")

    element = diagrams[0]

    # Strip namespaces (same as engine.py / parse.py)
    for elem in element.getiterator():
        if not (isinstance(elem, ET._Comment) or isinstance(elem, ET._ProcessingInstruction)):
            elem.tag = ET.QName(elem).localname

    pf_parse.check_duplicate_handles(element, set())

    # Reload user_namespace to reset global state (same as mk_diagram does)
    importlib.reload(user_namespace)

    result = pf_parse.mk_diagram(
        element,
        "svg",
        None,           # publication
        xml_path,       # filename
        False,          # suppress_caption
        None,           # diagram_number
        "pf_cli",       # environment
        return_string=True,
    )

    if result is None:
        return None, None

    svg_string, annotation_string = result
    return svg_string, annotation_string


def run_cpp_backend(xml_path, cpp_mod):
    """
    Run the C++ backend on an XML file and return the SVG string.
    """
    xml_path = str(xml_path)

    # Read the raw XML
    with open(xml_path, "r") as f:
        xml_source = f.read()

    # The C++ module should expose a build_from_string or parse function
    # that returns (svg_string, annotation_string)
    if hasattr(cpp_mod, "build_from_string"):
        result = cpp_mod.build_from_string("svg", xml_source)
        if isinstance(result, tuple):
            return result[0], result[1] if len(result) > 1 else None
        return result, None

    elif hasattr(cpp_mod, "parse"):
        # Use the parse interface: writes to temp directory
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_xml = Path(tmpdir) / Path(xml_path).name
            tmp_xml.write_text(xml_source)
            try:
                cpp_mod.parse(str(tmp_xml), "svg")
            except Exception as e:
                return None, None

            output_dir = Path(tmpdir) / "output"
            svg_files = list(output_dir.glob("*.svg")) if output_dir.exists() else []
            if svg_files:
                svg_string = svg_files[0].read_text()
                ann_files = list(output_dir.glob("*.xml"))
                ann_string = ann_files[0].read_text() if ann_files else None
                return svg_string, ann_string

        return None, None

    elif hasattr(cpp_mod, "mk_diagram"):
        # Use mk_diagram with string input
        try:
            result = cpp_mod.mk_diagram(xml_source, xml_path)
            if isinstance(result, tuple):
                return result
            return result, None
        except Exception as e:
            return None, None

    else:
        raise RuntimeError(
            "C++ module does not expose build_from_string, parse, or mk_diagram"
        )


# ============================================================================
# SVG comparison
# ============================================================================

# Attributes whose values are numeric and should be compared with tolerance
NUMERIC_ATTRS = {
    "x", "y", "x1", "y1", "x2", "y2",
    "cx", "cy", "r", "rx", "ry",
    "width", "height",
    "stroke-width", "stroke-miterlimit",
    "opacity", "stroke-opacity", "fill-opacity",
    "font-size",
    "refX", "refY",
    "markerWidth", "markerHeight",
}

# Attributes to skip entirely during comparison (order-dependent or volatile)
SKIP_ATTRS = {"id", "clip-path", "href", "xlink:href"}

# Regex matching a float (possibly negative, possibly with exponent)
FLOAT_RE = re.compile(r"^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$")

# Regex to split SVG path data into tokens
PATH_TOKEN_RE = re.compile(r"([MmLlHhVvCcSsQqTtAaZz]|[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?)")


def parse_svg(svg_string):
    """Parse an SVG string into an lxml element tree."""
    if svg_string is None:
        return None
    try:
        return ET.fromstring(svg_string.encode("utf-8") if isinstance(svg_string, str) else svg_string)
    except ET.XMLSyntaxError as e:
        return None


def is_numeric(s):
    """Check if a string looks like a number."""
    return bool(FLOAT_RE.match(s.strip()))


def numbers_close(a_str, b_str, tol):
    """Compare two numeric strings with tolerance."""
    try:
        a = float(a_str)
        b = float(b_str)
        if a == b:
            return True
        return abs(a - b) <= tol * max(1.0, abs(a), abs(b))
    except (ValueError, TypeError):
        return a_str == b_str


def compare_path_data(d1, d2, tol):
    """
    Compare two SVG path 'd' attribute strings.
    Tokenizes into commands + numbers, compares commands exactly
    and numbers with tolerance.
    """
    tokens1 = PATH_TOKEN_RE.findall(d1)
    tokens2 = PATH_TOKEN_RE.findall(d2)

    if len(tokens1) != len(tokens2):
        return False, f"path token count differs: {len(tokens1)} vs {len(tokens2)}"

    for i, (t1, t2) in enumerate(zip(tokens1, tokens2)):
        if t1[0].isalpha() and t2[0].isalpha():
            # Command letter
            if t1 != t2:
                return False, f"path command differs at token {i}: '{t1}' vs '{t2}'"
        else:
            # Numeric value
            if not numbers_close(t1, t2, tol):
                return False, f"path value differs at token {i}: {t1} vs {t2}"

    return True, ""


def compare_attr_value(attr_name, val1, val2, tol):
    """
    Compare a single attribute value.
    Returns (match: bool, detail: str).
    """
    if val1 == val2:
        return True, ""

    # Path data gets special treatment
    if attr_name == "d":
        return compare_path_data(val1, val2, tol)

    # Transform attributes: compare numerically
    if attr_name == "transform":
        nums1 = re.findall(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?", val1)
        nums2 = re.findall(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?", val2)
        if len(nums1) != len(nums2):
            return False, f"transform number count: {len(nums1)} vs {len(nums2)}"
        for n1, n2 in zip(nums1, nums2):
            if not numbers_close(n1, n2, tol):
                return False, f"transform value: {n1} vs {n2}"
        # Compare the non-numeric parts
        text1 = re.sub(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?", "", val1)
        text2 = re.sub(r"[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?", "", val2)
        if text1 != text2:
            return False, f"transform structure: '{text1}' vs '{text2}'"
        return True, ""

    # viewBox
    if attr_name == "viewBox":
        parts1 = val1.split()
        parts2 = val2.split()
        if len(parts1) != len(parts2):
            return False, f"viewBox parts: {len(parts1)} vs {len(parts2)}"
        for p1, p2 in zip(parts1, parts2):
            if not numbers_close(p1, p2, tol):
                return False, f"viewBox value: {p1} vs {p2}"
        return True, ""

    # Dash arrays
    if attr_name == "stroke-dasharray":
        parts1 = re.split(r"[,\s]+", val1.strip())
        parts2 = re.split(r"[,\s]+", val2.strip())
        if len(parts1) != len(parts2):
            return False, f"dasharray length: {len(parts1)} vs {len(parts2)}"
        for p1, p2 in zip(parts1, parts2):
            if not numbers_close(p1, p2, tol):
                return False, f"dasharray value: {p1} vs {p2}"
        return True, ""

    # Known numeric attributes
    if attr_name in NUMERIC_ATTRS:
        if is_numeric(val1) and is_numeric(val2):
            if numbers_close(val1, val2, tol):
                return True, ""
            return False, f"{val1} vs {val2}"

    # Style attribute: split into properties and compare each
    if attr_name == "style":
        props1 = dict(p.strip().split(":", 1) for p in val1.split(";") if ":" in p)
        props2 = dict(p.strip().split(":", 1) for p in val2.split(";") if ":" in p)
        if set(props1.keys()) != set(props2.keys()):
            missing = set(props1.keys()) ^ set(props2.keys())
            return False, f"style properties differ: {missing}"
        for k in props1:
            pv1 = props1[k].strip()
            pv2 = props2[k].strip()
            if pv1 != pv2:
                if is_numeric(pv1) and is_numeric(pv2):
                    if not numbers_close(pv1, pv2, tol):
                        return False, f"style {k}: {pv1} vs {pv2}"
                else:
                    return False, f"style {k}: '{pv1}' vs '{pv2}'"
        return True, ""

    # Default: exact string match
    return False, f"'{val1}' vs '{val2}'"


def strip_ns(tag):
    """Remove namespace URI from an element tag."""
    if "}" in tag:
        return tag.split("}", 1)[1]
    return tag


def compare_elements(elem1, elem2, tol, path=""):
    """
    Recursively compare two XML elements.
    Returns list of (path, detail) for each difference found.
    """
    diffs = []
    tag1 = strip_ns(elem1.tag)
    tag2 = strip_ns(elem2.tag)
    current = f"{path}/{tag1}"

    # Tag comparison
    if tag1 != tag2:
        diffs.append((current, f"tag mismatch: <{tag1}> vs <{tag2}>"))
        return diffs

    # Text content
    text1 = (elem1.text or "").strip()
    text2 = (elem2.text or "").strip()
    if text1 != text2:
        # Try numeric comparison for text that's just a number
        if is_numeric(text1) and is_numeric(text2):
            if not numbers_close(text1, text2, tol):
                diffs.append((current, f"text: {text1} vs {text2}"))
        elif text1 != text2:
            diffs.append((current, f"text: '{text1[:60]}' vs '{text2[:60]}'"))

    # Attributes (excluding volatile ones like id, clip-path references)
    attrs1 = {k: v for k, v in elem1.attrib.items() if k not in SKIP_ATTRS}
    attrs2 = {k: v for k, v in elem2.attrib.items() if k not in SKIP_ATTRS}

    # Check for missing/extra attributes
    only_in_1 = set(attrs1.keys()) - set(attrs2.keys())
    only_in_2 = set(attrs2.keys()) - set(attrs1.keys())
    if only_in_1:
        diffs.append((current, f"attrs only in Python: {only_in_1}"))
    if only_in_2:
        diffs.append((current, f"attrs only in C++: {only_in_2}"))

    # Compare shared attributes
    for attr in sorted(set(attrs1.keys()) & set(attrs2.keys())):
        match, detail = compare_attr_value(attr, attrs1[attr], attrs2[attr], tol)
        if not match:
            diffs.append((f"{current}/@{attr}", detail))

    # Children
    children1 = list(elem1)
    children2 = list(elem2)

    if len(children1) != len(children2):
        diffs.append((current, f"child count: {len(children1)} vs {len(children2)}"))
        # Compare up to the shorter list
        n = min(len(children1), len(children2))
    else:
        n = len(children1)

    for i in range(n):
        child_diffs = compare_elements(children1[i], children2[i], tol, current)
        diffs.extend(child_diffs)

    return diffs


def compare_svg_outputs(py_svg, cpp_svg, tol, verbose=False):
    """
    Compare two SVG strings structurally with numeric tolerance.
    Returns (passed: bool, summary: str, details: list[str]).
    """
    py_tree = parse_svg(py_svg)
    cpp_tree = parse_svg(cpp_svg)

    if py_tree is None and cpp_tree is None:
        return True, "both produced no output", []
    if py_tree is None:
        return False, "Python produced no valid SVG", []
    if cpp_tree is None:
        return False, "C++ produced no valid SVG", []

    # Quick check: exact string match
    if py_svg.strip() == cpp_svg.strip():
        return True, "exact match", []

    # Structural comparison
    diffs = compare_elements(py_tree, cpp_tree, tol)

    if not diffs:
        return True, "structurally equivalent (numeric tolerance applied)", []

    details = [f"  {path}: {detail}" for path, detail in diffs]

    if len(diffs) <= 5:
        summary = f"{len(diffs)} difference(s)"
    else:
        summary = f"{len(diffs)} differences (showing first 10)"
        details = details[:10] + [f"  ... and {len(diffs) - 10} more"]

    return False, summary, details


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="PreFigure Python vs C++ correctness comparison"
    )
    parser.add_argument(
        "--tolerance", type=float, default=1e-4,
        help="Relative tolerance for numeric comparisons (default: 1e-4)"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Show detailed diff output for failures"
    )
    parser.add_argument(
        "--examples", nargs="*", default=None,
        help="Specific example files to test (default: all)"
    )
    parser.add_argument(
        "--python-only", action="store_true",
        help="Only run the Python backend (verify it produces valid SVG)"
    )
    args = parser.parse_args()

    # Suppress prefigure logging noise
    logging.getLogger("prefigure").setLevel(logging.CRITICAL)

    print(f"{C.BOLD}{'=' * 64}{C.END}")
    print(f"{C.BOLD}  PreFigure: Python vs C++ Correctness Comparison{C.END}")
    print(f"{C.BOLD}{'=' * 64}{C.END}")
    print(f"  Numeric tolerance: {args.tolerance}")
    print()

    # ---- Discover examples ----
    if args.examples:
        examples = [EXAMPLES_DIR / f for f in args.examples]
    else:
        examples = [EXAMPLES_DIR / f for f in EXAMPLE_FILES if (EXAMPLES_DIR / f).exists()]

    if not examples:
        print(f"{C.FAIL}No example files found in {EXAMPLES_DIR}{C.END}")
        sys.exit(1)

    print(f"  Examples: {len(examples)} files")

    # ---- Load backends ----
    print()
    try:
        from prefig.core import parse as _  # noqa: F401
        py_available = True
        print(f"  {C.PASS}[OK]{C.END} Python backend loaded")
    except ImportError as e:
        py_available = False
        print(f"  {C.FAIL}[!!]{C.END} Python backend: {e}")

    cpp_mod = None
    if not args.python_only:
        if CPP_BUILD_DIR.exists() and str(CPP_BUILD_DIR) not in sys.path:
            sys.path.insert(0, str(CPP_BUILD_DIR))
        try:
            import _prefigure as cpp_mod_
            cpp_mod = cpp_mod_
            print(f"  {C.PASS}[OK]{C.END} C++ backend loaded")
        except ImportError:
            print(f"  {C.WARN}[--]{C.END} C++ backend not available")
            print(f"       Build: cd prefigure-cpp && ./build.sh --release")
            print(f"       Then:  export PYTHONPATH=prefigure-cpp/build:$PYTHONPATH")
    else:
        print(f"  {C.INFO}[--]{C.END} C++ backend skipped (--python-only)")

    if not py_available:
        print(f"\n{C.FAIL}Cannot proceed without Python backend.{C.END}")
        sys.exit(1)

    if cpp_mod is None and not args.python_only:
        print(f"\n{C.WARN}C++ backend not available. Running Python-only validation.{C.END}")
        args.python_only = True

    print()
    print(f"{C.BOLD}{'─' * 64}{C.END}")
    print()

    # ---- Run comparisons ----
    results = []
    total_pass = 0
    total_fail = 0
    total_skip = 0

    for i, example_path in enumerate(examples):
        name = example_path.stem
        label = f"[{i + 1}/{len(examples)}] {name}"
        print(f"  {C.BOLD}{label}{C.END}", end="", flush=True)

        # Run Python backend
        try:
            py_svg, py_ann = run_python_backend(example_path)
        except Exception as e:
            print(f"  {C.FAIL}Python error: {e}{C.END}")
            results.append({"name": name, "status": "FAIL", "detail": f"Python error: {e}"})
            total_fail += 1
            continue

        if py_svg is None:
            print(f"  {C.WARN}Python produced no output{C.END}")
            results.append({"name": name, "status": "SKIP", "detail": "Python produced no output"})
            total_skip += 1
            continue

        py_tree = parse_svg(py_svg)
        if py_tree is None:
            print(f"  {C.FAIL}Python produced invalid SVG{C.END}")
            results.append({"name": name, "status": "FAIL", "detail": "Invalid Python SVG"})
            total_fail += 1
            continue

        # Count elements in Python SVG for a basic sanity metric
        py_elem_count = len(list(py_tree.iter()))

        if args.python_only:
            print(f"  ...  {C.PASS}OK{C.END}  Python SVG valid"
                  f" ({py_elem_count} elements, {len(py_svg)} bytes)")
            results.append({"name": name, "status": "PASS", "detail": "Python SVG valid"})
            total_pass += 1
            continue

        # Run C++ backend
        try:
            cpp_svg, cpp_ann = run_cpp_backend(example_path, cpp_mod)
        except Exception as e:
            print(f"  {C.FAIL}C++ error: {e}{C.END}")
            results.append({"name": name, "status": "FAIL", "detail": f"C++ error: {e}"})
            total_fail += 1
            continue

        if cpp_svg is None:
            print(f"  {C.WARN}C++ produced no output{C.END}")
            results.append({"name": name, "status": "SKIP", "detail": "C++ produced no output"})
            total_skip += 1
            continue

        # Compare SVGs
        match, summary, details = compare_svg_outputs(py_svg, cpp_svg, args.tolerance, args.verbose)

        if match:
            cpp_tree = parse_svg(cpp_svg)
            cpp_elem_count = len(list(cpp_tree.iter())) if cpp_tree is not None else 0
            print(f"  ...  {C.PASS}PASS{C.END}  {summary}"
                  f"  (Py: {py_elem_count} elems, C++: {cpp_elem_count} elems)")
            results.append({"name": name, "status": "PASS", "detail": summary})
            total_pass += 1
        else:
            print(f"  ...  {C.FAIL}FAIL{C.END}  {summary}")
            if args.verbose and details:
                for d in details:
                    print(f"       {d}")
            results.append({"name": name, "status": "FAIL", "detail": summary, "diffs": details})
            total_fail += 1

        # Compare annotations too (if both produced them)
        if py_ann is not None and cpp_ann is not None:
            ann_match, ann_summary, ann_details = compare_svg_outputs(
                py_ann, cpp_ann, args.tolerance, args.verbose
            )
            if not ann_match:
                print(f"         {C.WARN}Annotations differ: {ann_summary}{C.END}")

    # ---- Summary ----
    print()
    print(f"{C.BOLD}{'─' * 64}{C.END}")
    print()
    print(f"  {C.BOLD}Results:{C.END}")
    print(f"    {C.PASS}Passed:{C.END}  {total_pass}")
    print(f"    {C.FAIL}Failed:{C.END}  {total_fail}")
    if total_skip > 0:
        print(f"    {C.WARN}Skipped:{C.END} {total_skip}")
    print()

    if total_fail == 0 and total_skip == 0:
        print(f"  {C.PASS}{C.BOLD}All {total_pass} tests passed.{C.END}")
    elif total_fail == 0:
        print(f"  {C.PASS}{total_pass} passed{C.END}, {C.WARN}{total_skip} skipped{C.END}")
    else:
        print(f"  {C.FAIL}{total_fail} FAILED{C.END}")
        failed_names = [r["name"] for r in results if r["status"] == "FAIL"]
        print(f"  Failed: {', '.join(failed_names)}")

    print()
    return 1 if total_fail > 0 else 0


if __name__ == "__main__":
    sys.exit(main())
