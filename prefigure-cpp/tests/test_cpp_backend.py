#!/usr/bin/env python3
"""
Python integration tests for the PreFigure C++ backend.

Run from the prefigure-cpp directory after building with Python bindings:
    cmake -B build -S . -DPREFIGURE_BUILD_PYTHON=ON
    cmake --build build --parallel
    PYTHONPATH=build pytest tests/test_cpp_backend.py -v
"""

import os
import sys
import pytest
from pathlib import Path

# Add build directory to path
BUILD_DIR = Path(__file__).parent.parent / "build"
if BUILD_DIR.exists() and str(BUILD_DIR) not in sys.path:
    sys.path.insert(0, str(BUILD_DIR))

RESOURCES_DIR = Path(__file__).parent / "resources"


def has_prefigure():
    """Check if the C++ module is importable."""
    try:
        import _prefigure
        return True
    except ImportError:
        return False


# Skip all tests if C++ module not available
pytestmark = pytest.mark.skipif(
    not has_prefigure(),
    reason="C++ _prefigure module not built (build with -DPREFIGURE_BUILD_PYTHON=ON)"
)


class TestModuleImport:
    """Test basic module import and type availability."""

    def test_import(self):
        import _prefigure
        assert _prefigure is not None

    def test_enums_available(self):
        import _prefigure
        assert hasattr(_prefigure, 'OutputFormat')
        assert hasattr(_prefigure, 'Environment')
        assert hasattr(_prefigure, 'OutlineStatus')

    def test_output_format_values(self):
        import _prefigure
        assert _prefigure.OutputFormat.SVG is not None
        assert _prefigure.OutputFormat.Tactile is not None

    def test_environment_values(self):
        import _prefigure
        assert _prefigure.Environment.Pretext is not None
        assert _prefigure.Environment.PfCli is not None
        assert _prefigure.Environment.Pyodide is not None

    def test_parse_function_available(self):
        import _prefigure
        assert hasattr(_prefigure, 'parse')
        assert callable(_prefigure.parse)


class TestParsePipeline:
    """Test the full parse pipeline on example XML files."""

    def test_parse_tangent(self, tmp_path):
        """Parse tangent.xml and verify SVG output."""
        import _prefigure
        import shutil

        xml_path = RESOURCES_DIR / "tangent.xml"
        if not xml_path.exists():
            pytest.skip("tangent.xml not in test resources")

        # Copy to temp dir so output goes there
        tmp_xml = tmp_path / "tangent.xml"
        shutil.copy(xml_path, tmp_xml)

        _prefigure.parse(
            str(tmp_xml),
            _prefigure.OutputFormat.SVG,
            "",
            False,
            _prefigure.Environment.PfCli
        )

        output = tmp_path / "output" / "tangent.svg"
        assert output.exists(), f"Expected SVG output at {output}"
        content = output.read_text()
        assert "<svg" in content
        assert len(content) > 100  # Non-trivial SVG

    def test_parse_derivatives(self, tmp_path):
        """Parse derivatives.xml and verify SVG output."""
        import _prefigure
        import shutil

        xml_path = RESOURCES_DIR / "derivatives.xml"
        if not xml_path.exists():
            pytest.skip("derivatives.xml not in test resources")

        tmp_xml = tmp_path / "derivatives.xml"
        shutil.copy(xml_path, tmp_xml)

        _prefigure.parse(
            str(tmp_xml),
            _prefigure.OutputFormat.SVG,
            "",
            False,
            _prefigure.Environment.PfCli
        )

        output = tmp_path / "output" / "derivatives.svg"
        assert output.exists()

    def test_parse_with_default_args(self, tmp_path):
        """Test parse with default arguments."""
        import _prefigure
        import shutil

        xml_path = RESOURCES_DIR / "tangent.xml"
        if not xml_path.exists():
            pytest.skip("tangent.xml not in test resources")

        tmp_xml = tmp_path / "tangent.xml"
        shutil.copy(xml_path, tmp_xml)

        # Use defaults for format, pub_file, suppress_caption, environment
        _prefigure.parse(str(tmp_xml))

        output = tmp_path / "output" / "tangent.svg"
        assert output.exists()


class TestOutputFormat:
    """Test different output formats."""

    def test_svg_format(self, tmp_path):
        """Verify SVG output format produces valid SVG."""
        import _prefigure
        import shutil

        xml_path = RESOURCES_DIR / "tangent.xml"
        if not xml_path.exists():
            pytest.skip("tangent.xml not in test resources")

        tmp_xml = tmp_path / "tangent.xml"
        shutil.copy(xml_path, tmp_xml)

        _prefigure.parse(
            str(tmp_xml),
            _prefigure.OutputFormat.SVG
        )

        output = tmp_path / "output" / "tangent.svg"
        assert output.exists()

        content = output.read_text()
        # SVG should have xmlns
        assert "svg" in content.lower()
        # Should have viewBox or width/height
        assert "width" in content or "viewBox" in content


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
