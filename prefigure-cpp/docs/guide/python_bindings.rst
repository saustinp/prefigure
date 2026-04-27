Python Bindings
===============

The C++ library is exposed to Python via `pybind11 <https://pybind11.readthedocs.io/>`_.
The binding module is named ``_prefigure`` and provides a thin wrapper over the
core C++ functions.

Design Strategy
---------------

The boundary between Python and C++ uses **XML strings** rather than shared
DOM objects. This avoids the complexity of bridging lxml (Python) with
pugixml (C++) at the type level:

1. Python converts ``lxml.etree.Element`` to a string
2. The string is passed to C++ via pybind11
3. C++ parses it with pugixml, processes it, and returns
   ``(svg_string, optional<annotations_string>)``
4. Python receives the tuple ``(svg, annotations_or_None)`` (or, for the
   file-based ``parse()`` entry point, C++ writes outputs to disk)

This approach is simple, safe, and avoids memory ownership ambiguity.

Module Structure
----------------

The binding code is in ``bindings/``:

- ``module.cpp`` — ``PYBIND11_MODULE(_prefigure, m)`` entry point
- ``py_types.cpp`` — binds ``OutputFormat``, ``Environment``, ``OutlineStatus`` enums
- ``py_parse.cpp`` — binds ``parse()`` and ``mk_diagram()``
- ``py_diagram.cpp`` — binds the ``Diagram`` class (for advanced use)

Building the Python Module
--------------------------

::

    cmake -B build -S . -DPREFIGURE_BUILD_PYTHON=ON
    cmake --build build --parallel

This produces ``build/_prefigure*.so`` (Linux) or ``build/_prefigure*.pyd`` (Windows).

To make it importable::

    export PYTHONPATH=/path/to/prefigure-cpp/build:$PYTHONPATH

Usage from Python
-----------------

::

    import _prefigure

    # Build a diagram from an XML file
    _prefigure.parse(
        "path/to/diagram.xml",
        _prefigure.OutputFormat.SVG,
        "",      # publication file (empty = none)
        False,   # suppress caption
        _prefigure.Environment.PfCli
    )

    # Or use as a drop-in replacement for the Python backend:
    # The existing cli.py and engine.py can import from the C++ module
    # instead of prefig.core.parse

Thin Python Wrapper
-------------------

For seamless integration with the existing Python CLI, a wrapper module can
convert between lxml elements and strings::

    # prefig/core/_cpp_backend.py
    import _prefigure
    import lxml.etree as ET

    def build_from_string(format_str, xml_string, environment="pyodide"):
        # Returns (svg_string, annotations_string_or_None).
        return _prefigure.build_from_string(format_str, xml_string, environment)

    def parse(filename, format_str="svg", pub_file="",
              suppress_caption=False, environment="pretext"):
        _prefigure.parse(filename, format_str, pub_file,
                         suppress_caption, environment)

Extending the Bindings
----------------------

To expose a new C++ function to Python:

1. Declare the function in a header (e.g., ``include/prefigure/mymodule.hpp``)
2. Implement it in ``src/mymodule.cpp``
3. Add the binding in ``bindings/py_mymodule.cpp``::

       void bind_mymodule(py::module_& m) {
           m.def("my_function", &prefigure::my_function,
                 py::arg("param1"), py::arg("param2") = default_value,
                 "Docstring for Python help()");
       }

4. Call ``bind_mymodule(m)`` from ``module.cpp``
