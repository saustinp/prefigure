#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "prefigure/diagram.hpp"

namespace py = pybind11;

void bind_diagram(py::module_& m) {
    py::class_<prefigure::Diagram>(m, "Diagram")
        .def("output_format", &prefigure::Diagram::output_format)
        .def("get_environment", &prefigure::Diagram::get_environment);
}
