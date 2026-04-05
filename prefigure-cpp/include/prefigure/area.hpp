#pragma once

#include "types.hpp"

namespace prefigure {

void area_between_curves(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void area_under_curve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
