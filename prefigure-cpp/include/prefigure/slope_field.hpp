#pragma once

#include "types.hpp"

namespace prefigure {

void slope_field(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void vector_field(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
