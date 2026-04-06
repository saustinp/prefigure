#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render an `<area-between-curves>` XML element as SVG.
 *
 * Shades the region between two function graphs over a specified domain
 * by tracing f forward and g backward, then closing the path.  Supports
 * both Cartesian and polar coordinate modes.
 *
 * Functions can be specified either as a comma-separated pair via `@functions`
 * (e.g. `functions="(f, g)"`) or individually via `@function1` and `@function2`.
 *
 * @par XML Attributes
 * - `functions` (required*): Two function names, e.g. "(f, g)" or "[f, g]".
 * - `function1`, `function2` (required*): Alternative to `functions`.
 * - `domain` (optional): Evaluation interval "(a, b)"; defaults to bbox x-range.
 * - `domain-degrees` (optional): "yes" to interpret domain in degrees (converted to radians).
 * - `coordinates` (optional): "polar" for polar coordinate mode.
 * - `N` (optional, default 100): Number of sample points per curve.
 * - `stroke` (optional, default "black"): Stroke color.
 * - `fill` (optional, default "lightgray"): Fill color.
 * - `thickness` (optional, default "2"): Stroke width.
 * - `outline` (optional): "yes" for outline rendering.
 *
 * @par SVG Output
 * Creates a filled `<path>` element enclosing the region between the two curves.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void area_between_curves(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render an `<area-under-curve>` XML element as SVG.
 *
 * Convenience wrapper around area_between_curves() that shades the region
 * between a function and the x-axis (y = 0).  Internally defines a zero
 * function and delegates to area_between_curves().
 *
 * @par XML Attributes
 * - `function` (required): The function name.
 * - `domain` (optional): Evaluation interval "(a, b)"; defaults to bbox x-range.
 * - `N` (optional, default 100): Number of sample points.
 * - `fill`, `stroke`, `thickness`: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a filled `<path>` element enclosing the region under the curve.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see area_between_curves()
 */
void area_under_curve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
