#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render an `<area-between-curves>` XML element as SVG.
 *
 * Shades the region between two function graphs over a specified domain.
 *
 * @par XML Attributes
 * - `functions` (required): Two function names separated by a comma.
 * - `domain` (optional): Evaluation interval "(a, b)"; defaults to bbox x-range.
 * - `fill` (optional): Fill color for the shaded region.
 * - `N` (optional): Number of sample points.
 *
 * @par SVG Output
 * Creates a filled `<path>` element enclosing the region between the two curves.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void area_between_curves(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render an `<area-under-curve>` XML element as SVG.
 *
 * Shades the region between a function graph and the x-axis over a
 * specified domain.
 *
 * @par XML Attributes
 * - `function` (required): The function name.
 * - `domain` (optional): Evaluation interval "(a, b)"; defaults to bbox x-range.
 * - `fill` (optional): Fill color for the shaded region.
 * - `N` (optional): Number of sample points.
 *
 * @par SVG Output
 * Creates a filled `<path>` element enclosing the region under the curve.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void area_under_curve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
