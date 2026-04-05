#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

void grid(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void grid_axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
bool is_axes_tag(const std::string& tag);

}  // namespace prefigure
