#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

void point(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

// Test whether a point p is inside a point marker of given style/size centered at center.
bool inside(const Point2d& p, const Point2d& center, double size,
            const std::string& style, const CTM& ctm, double buffer = 0.0);

}  // namespace prefigure
