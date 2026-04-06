#include <pybind11/pybind11.h>

#include <stdexcept>

namespace py = pybind11;

// Forward declarations of binding registration functions
void bind_types(py::module_& m);
void bind_diagram(py::module_& m);
void bind_parse(py::module_& m);

PYBIND11_MODULE(_prefigure, m) {
    m.doc() = "PreFigure C++ backend — high-performance diagram rendering";

    // Register C++ exceptions as Python exceptions
    py::register_exception<std::runtime_error>(m, "PrefigureError");

    bind_types(m);
    bind_diagram(m);
    bind_parse(m);
}
