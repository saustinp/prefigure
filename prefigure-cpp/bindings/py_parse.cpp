#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "prefigure/parse.hpp"

namespace py = pybind11;

void bind_parse(py::module_& m) {
    m.def("parse", &prefigure::parse,
          py::arg("filename"),
          py::arg("format") = prefigure::OutputFormat::SVG,
          py::arg("pub_file") = "",
          py::arg("suppress_caption") = false,
          py::arg("environment") = prefigure::Environment::Pretext,
          "Parse a PreFigure XML file and render all diagrams to SVG");

    m.def("build_from_string", &prefigure::build_from_string,
          py::arg("format"),
          py::arg("xml_string"),
          py::arg("environment") = "pyodide",
          "Parse an XML string and return SVG output as a string");
}
