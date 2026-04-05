#pragma once

#include "types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace prefigure {

/**
 * @brief Parse polygon vertices from an XML element's attributes.
 *
 * Supports two modes:
 * - Direct: evaluates the `@points` attribute as a flat vector of coordinates.
 * - Parametric: uses `@parameter` to generate points from an expression.
 *
 * @param element The source XML element.
 * @param diagram The diagram context for expression evaluation.
 * @return A vector of 2D points, or nullopt on parse failure.
 */
std::optional<std::vector<Point2d>> parse_polygon_points(XmlNode element, Diagram& diagram);

/**
 * @brief Render a `<polygon>` XML element as SVG.
 *
 * Draws a closed polygon through a list of vertices.  Supports optional
 * arrowheads, outline rendering, and label attachment.
 *
 * @par XML Attributes
 * - `points` (required unless `parameter` is given): Flat coordinate vector.
 * - `parameter` (optional): Parametric generation mode.
 * - `closed` (optional, default: "yes"): Whether to close the polygon path.
 * - `arrows` (optional): Arrowhead configuration.
 * - `stroke`, `fill`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` element with "M ... L ... Z" commands.
 *
 * @param element         Source XML element.
 * @param diagram         Parent diagram context.
 * @param parent          SVG parent node for appending output.
 * @param status          Outline rendering pass.
 * @param points_override If non-empty, use these points instead of parsing from attributes.
 * @param arrow_points    If non-empty, additional points used for arrowhead direction computation.
 *
 * @see parse_polygon_points()
 */
void polygon_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status,
                     const std::vector<Point2d>& points_override = {},
                     const std::vector<Point2d>& arrow_points = {});

/**
 * @brief Render a `<spline>` XML element as SVG.
 *
 * Draws a smooth curve through control points using cubic Bezier spline
 * interpolation.
 *
 * @par XML Attributes
 * - `points` (required): Control point coordinates.
 * - `stroke`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` with cubic Bezier curve commands.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void spline(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<triangle>` XML element as SVG.
 *
 * Convenience wrapper that creates a closed polygon from three vertices.
 *
 * @par XML Attributes
 * - `vertices` (required): Three vertex coordinates.
 * - `stroke`, `fill`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` element forming a closed triangle.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void triangle(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
