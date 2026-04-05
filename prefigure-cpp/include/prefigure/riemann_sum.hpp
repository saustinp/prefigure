#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<riemann-sum>` XML element as SVG.
 *
 * Draws a collection of rectangles approximating the area under a curve,
 * illustrating left, right, midpoint, or other Riemann sum types.
 *
 * @par XML Attributes
 * - `function` (required): The function name to approximate.
 * - `domain` (required): Evaluation interval "(a, b)".
 * - `N` (required): Number of rectangles.
 * - `type` (optional, default: "left"): Sum type ("left", "right", "midpoint").
 * - `fill`, `stroke`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates multiple `<rect>` or `<path>` elements for the rectangles.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void riemann_sum(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
