#pragma once

#include "types.hpp"

#ifdef PREFIGURE_HAS_SHAPES

namespace prefigure {

void shape_define(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void shape(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure

#endif  // PREFIGURE_HAS_SHAPES
