#pragma once

#include "types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace prefigure {

// Parse points from element attributes (direct or parametric generation).
std::optional<std::vector<Point2d>> parse_polygon_points(XmlNode element, Diagram& diagram);

void polygon_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status,
                     const std::vector<Point2d>& points_override = {},
                     const std::vector<Point2d>& arrow_points = {});

void spline(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void triangle(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
