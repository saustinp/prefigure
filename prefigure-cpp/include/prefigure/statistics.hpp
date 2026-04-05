#pragma once

#include "types.hpp"

namespace prefigure {

void scatter(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void histogram(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
