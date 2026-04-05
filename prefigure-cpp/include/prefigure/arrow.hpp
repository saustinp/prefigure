#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

void add_arrowhead_to_path(Diagram& diagram, const std::string& path_data, XmlNode element);
double get_arrow_length(const std::string& arrow_style);

}  // namespace prefigure
