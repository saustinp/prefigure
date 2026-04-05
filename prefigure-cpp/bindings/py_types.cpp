#include <pybind11/pybind11.h>
#include "prefigure/types.hpp"

namespace py = pybind11;

void bind_types(py::module_& m) {
    py::enum_<prefigure::OutputFormat>(m, "OutputFormat")
        .value("SVG", prefigure::OutputFormat::SVG)
        .value("Tactile", prefigure::OutputFormat::Tactile);

    py::enum_<prefigure::Environment>(m, "Environment")
        .value("Pretext", prefigure::Environment::Pretext)
        .value("PfCli", prefigure::Environment::PfCli)
        .value("Pyodide", prefigure::Environment::Pyodide);

    py::enum_<prefigure::OutlineStatus>(m, "OutlineStatus")
        .value("NONE", prefigure::OutlineStatus::None)
        .value("AddOutline", prefigure::OutlineStatus::AddOutline)
        .value("FinishOutline", prefigure::OutlineStatus::FinishOutline);
}
