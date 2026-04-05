#pragma once

#include "types.hpp"

namespace prefigure {

void polygon_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void spline(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void triangle(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
