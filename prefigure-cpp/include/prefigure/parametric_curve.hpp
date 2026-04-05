#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<parametric-curve>` XML element as SVG.
 *
 * Plots a parametric curve (x(t), y(t)) over a specified parameter range.
 * The functions are evaluated from the expression namespace.
 *
 * @par XML Attributes
 * - `function` (required): A function expression returning a 2D point for each parameter value.
 * - `domain` (required): Parameter range "(t_min, t_max)".
 * - `N` (optional): Number of sample points.
 * - `arrows` (optional): Arrowhead configuration.
 * - `stroke`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` element tracing the parametric curve.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void parametric_curve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
