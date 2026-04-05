#pragma once

#include "types.hpp"

#include <optional>
#include <string>
#include <utility>

namespace prefigure {

void line(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

// Create an SVG <line> element. If user_coords is true, transforms p0/p1 through diagram.
// endpoint_offsets is an Eigen::VectorXd (1D: along direction) or empty.
XmlNode mk_line(const Point2d& p0, const Point2d& p1, Diagram& diagram,
                const std::string& id = "",
                const Eigen::VectorXd& endpoint_offsets = Eigen::VectorXd(),
                bool user_coords = true);

// Find the intersection of an infinite line with the bounding box.
// Returns nullopt if the line doesn't intersect the bbox.
std::optional<std::pair<Point2d, Point2d>> infinite_line(
    const Point2d& p0, const Point2d& p1, Diagram& diagram,
    std::optional<double> slope = std::nullopt);

}  // namespace prefigure
