#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "prefigure/diagram.hpp"

namespace py = pybind11;

void bind_diagram(py::module_& m) {
    py::class_<prefigure::Diagram>(m, "Diagram")
        .def("begin_figure", &prefigure::Diagram::begin_figure)
        .def("parse", &prefigure::Diagram::parse,
             py::arg("element") = std::nullopt,
             py::arg("root") = std::nullopt,
             py::arg("outline_status") = prefigure::OutlineStatus::None)
        .def("place_labels", &prefigure::Diagram::place_labels)
        .def("end_figure", &prefigure::Diagram::end_figure)
        .def("end_figure_to_string", &prefigure::Diagram::end_figure_to_string)
        .def("annotate_source", &prefigure::Diagram::annotate_source)
        .def("output_format", &prefigure::Diagram::output_format)
        .def("get_environment", &prefigure::Diagram::get_environment);
}
