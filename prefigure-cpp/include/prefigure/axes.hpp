#pragma once

#include "types.hpp"

namespace prefigure {

void axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void tick_mark(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
