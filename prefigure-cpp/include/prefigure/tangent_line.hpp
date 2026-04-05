#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<tangent-line>` XML element as SVG.
 *
 * Draws the tangent line to a function at a specified point.  Computes
 * the derivative numerically and draws a line segment through the
 * tangent point with the computed slope.
 *
 * @par XML Attributes
 * - `function` (required): Name of the function to differentiate.
 * - `point` (required): The x-coordinate at which to draw the tangent.
 * - `length` (optional): Length of the tangent line segment.
 * - `stroke`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<line>` or `<path>` element representing the tangent line.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void tangent(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
