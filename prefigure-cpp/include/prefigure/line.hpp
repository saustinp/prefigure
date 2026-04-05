#pragma once

#include "types.hpp"

#include <optional>
#include <string>
#include <utility>

namespace prefigure {

/**
 * @brief Render a `<line>` XML element as SVG.
 *
 * Draws a straight line segment between two points.  Supports endpoint
 * specification via `@endpoints` (4-element vector) or separate `@p1`/`@p2`
 * attributes.  Can optionally extend to infinite (clipped to bbox), add
 * arrowheads, and attach labels.
 *
 * @par XML Attributes
 * - `endpoints` (optional): A 4-element vector "(x1,y1,x2,y2)".
 * - `p1`, `p2` (alternative): Individual endpoint expressions.
 * - `infinite` (optional, default: "no"): If "yes", extend the line to the bounding box.
 * - `arrows` (optional, default: "0"): Number of arrowheads (0, 1, or 2).
 * - `stroke`, `thickness`, `dash`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<line>` element (or `<path>` for infinite lines) with stroke styling.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see mk_line(), infinite_line()
 */
void line(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Create an SVG `<line>` element from two endpoints.
 *
 * If @p user_coords is true (default), the points are transformed through
 * the diagram's CTM.  Endpoint offsets can shorten or lengthen the line
 * (used for arrowhead clearance).
 *
 * @param p0               First endpoint in user coordinates.
 * @param p1               Second endpoint in user coordinates.
 * @param diagram          The diagram context for coordinate transforms.
 * @param id               Optional ID to assign to the SVG element.
 * @param endpoint_offsets  If non-empty, a vector of offsets applied along the line direction.
 * @param user_coords      If true, transform p0/p1 through the diagram's CTM.
 * @return The created SVG `<line>` node.
 */
XmlNode mk_line(const Point2d& p0, const Point2d& p1, Diagram& diagram,
                const std::string& id = "",
                const Eigen::VectorXd& endpoint_offsets = Eigen::VectorXd(),
                bool user_coords = true);

/**
 * @brief Compute the intersection of an infinite line with the diagram's bounding box.
 *
 * Given two points defining a line and the diagram's current bbox, finds
 * where the line enters and exits the visible region.
 *
 * @param p0      A point on the line.
 * @param p1      Another point on the line (defining direction).
 * @param diagram The diagram context providing the bounding box.
 * @param slope   Optional explicit slope (overrides the slope derived from p0/p1).
 * @return A pair of clipped endpoints, or nullopt if the line does not intersect the bbox.
 */
std::optional<std::pair<Point2d, Point2d>> infinite_line(
    const Point2d& p0, const Point2d& p1, Diagram& diagram,
    std::optional<double> slope = std::nullopt);

}  // namespace prefigure
