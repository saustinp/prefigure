#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<slope-field>` XML element as SVG.
 *
 * Draws short line segments at a regular grid of points, each with slope
 * determined by a function f(x, y), visualizing the direction field of
 * a first-order ODE dy/dx = f(x, y).
 *
 * @par XML Attributes
 * - `function` (required): A two-argument function f(x, y) giving the slope.
 * - `spacings` (optional): Grid spacing "(dx, dy)".
 * - `length` (optional): Length of each line segment.
 * - `stroke`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates multiple `<line>` elements arranged in a grid pattern.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void slope_field(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<vector-field>` XML element as SVG.
 *
 * Draws arrows at a regular grid of points, each showing the vector
 * value of a function F(x, y) = (u, v), visualizing a 2D vector field.
 *
 * @par XML Attributes
 * - `function` (required): A function returning a 2D vector at each point.
 * - `spacings` (optional): Grid spacing "(dx, dy)".
 * - `stroke`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates multiple `<line>` elements with arrowhead markers.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void vector_field(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
