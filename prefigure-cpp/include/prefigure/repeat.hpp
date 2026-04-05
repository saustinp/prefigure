#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

void repeat_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
std::string epub_clean(const std::string& text);

}  // namespace prefigure
