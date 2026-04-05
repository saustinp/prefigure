#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "prefigure/parse.hpp"

namespace py = pybind11;

void bind_parse(py::module_& m) {
    m.def("parse", &prefigure::parse,
          py::arg("source"),
          py::arg("filename") = "",
          py::arg("diagram_number") = std::nullopt,
          py::arg("format") = prefigure::OutputFormat::SVG,
          py::arg("output") = std::nullopt,
          py::arg("publication") = prefigure::XmlNode(),
          py::arg("suppress_caption") = false,
          py::arg("environment") = prefigure::Environment::Pretext,
          "Parse a PreFigure XML source and render a diagram");

    m.def("mk_diagram", &prefigure::mk_diagram,
          py::arg("source"),
          py::arg("filename") = "",
          py::arg("diagram_number") = std::nullopt,
          py::arg("format") = prefigure::OutputFormat::SVG,
          py::arg("output") = std::nullopt,
          py::arg("publication") = prefigure::XmlNode(),
          py::arg("suppress_caption") = false,
          py::arg("environment") = prefigure::Environment::Pretext,
          "Create a Diagram object from PreFigure XML source");
}
