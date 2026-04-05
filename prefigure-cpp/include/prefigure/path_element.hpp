#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

void path_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
bool is_path_tag(const std::string& tag);

}  // namespace prefigure
