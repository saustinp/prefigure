#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

/**
 * @brief Render a `<point>` XML element as SVG.
 *
 * Draws a point marker (circle, box, diamond, etc.) at a specified location.
 * Supports Cartesian and polar coordinate systems, multiple marker styles,
 * and optional labels with automatic alignment.
 *
 * @par XML Attributes
 * - `p` (required): Point location expression, e.g., "(1,2)".
 * - `coordinates` (optional, default: "cartesian"): "cartesian" or "polar".
 * - `style` (optional, default: "circle"): Marker shape ("circle", "box", "diamond", etc.).
 * - `size` (optional): Marker size in SVG pixels.
 * - `stroke`, `fill`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a marker shape (`<circle>`, `<path>`, etc.) at the transformed point.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void point(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Test whether a 2D point lies inside a point marker.
 *
 * Used for hit testing (e.g., label collision avoidance).
 *
 * @param p       The test point in SVG coordinates.
 * @param center  The marker center in user coordinates.
 * @param size    The marker size.
 * @param style   The marker style string (e.g., "circle", "box").
 * @param ctm     The coordinate transformation to apply to @p center.
 * @param buffer  Extra padding around the marker boundary (default: 0).
 * @return True if @p p is inside the marker (plus buffer).
 */
bool inside(const Point2d& p, const Point2d& center, double size,
            const std::string& style, const CTM& ctm, double buffer = 0.0);

}  // namespace prefigure
