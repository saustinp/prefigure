#!/usr/bin/env python3
"""
Generate golden SVG reference files from the Python PreFigure implementation.

Usage:
    source .venv/bin/activate  # from project root
    python prefigure-cpp/scripts/generate_golden.py

Outputs golden SVGs to prefigure-cpp/tests/golden/
"""

import importlib
import logging
import sys
from pathlib import Path

# Suppress prefigure logging noise
logging.getLogger("prefigure").setLevel(logging.CRITICAL)

PROJECT_ROOT = Path(__file__).parent.parent.parent
EXAMPLES_DIR = PROJECT_ROOT / "prefig" / "resources" / "examples"
GOLDEN_DIR = Path(__file__).parent.parent / "tests" / "golden"

EXAMPLES = [
    "tangent.xml",
    "derivatives.xml",
    "de-system.xml",
    "diffeqs.xml",
    "implicit.xml",
    "projection.xml",
    "riemann.xml",
    "roots_of_unity.xml",
]

sys.path.insert(0, str(PROJECT_ROOT))


def generate():
    from prefig.core import parse as pf_parse
    from prefig.core import user_namespace
    import lxml.etree as ET

    GOLDEN_DIR.mkdir(parents=True, exist_ok=True)

    for example in EXAMPLES:
        xml_path = EXAMPLES_DIR / example
        if not xml_path.exists():
            print(f"  SKIP {example} (not found)")
            continue

        name = xml_path.stem
        print(f"  Generating {name}.svg ...", end=" ", flush=True)

        try:
            tree = ET.parse(str(xml_path))
            ns = {"pf": "https://prefigure.org"}
            diagrams = tree.xpath("//pf:diagram", namespaces=ns) + tree.xpath("//diagram")

            if not diagrams:
                print("NO DIAGRAM")
                continue

            element = diagrams[0]
            for elem in element.getiterator():
                if not (isinstance(elem, ET._Comment) or isinstance(elem, ET._ProcessingInstruction)):
                    elem.tag = ET.QName(elem).localname

            pf_parse.check_duplicate_handles(element, set())
            importlib.reload(user_namespace)

            result = pf_parse.mk_diagram(
                element, "svg", None, str(xml_path),
                False, None, "pf_cli", return_string=True
            )

            if result is None:
                print("FAILED (no output)")
                continue

            svg_string, _ = result
            golden_path = GOLDEN_DIR / f"{name}.svg"
            golden_path.write_text(svg_string)
            print(f"OK ({len(svg_string)} bytes)")

        except Exception as e:
            print(f"ERROR: {e}")


if __name__ == "__main__":
    print("Generating golden SVG reference files from Python implementation...")
    print(f"Output: {GOLDEN_DIR}/")
    print()
    generate()
    print()
    print("Done.")
