#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<parametric-curve>` XML element as SVG.
 *
 * Evaluates a parametric function f(t) → (x, y) at N uniformly spaced
 * sample points over a parameter domain and renders the result as an
 * SVG path.  Supports closed paths, arrowheads, and custom arrow
 * placement along the curve.
 *
 * @par XML Attributes
 * - `function` (required): Expression name of a function returning a 2D point.
 * - `domain` (required): Parameter range "(t_min, t_max)".
 * - `N` (optional, default 100): Number of sample points.
 * - `closed` (optional, default "no"): "yes" to close the path with Z.
 * - `arrows` (optional, default "0"): "1" or "2" for arrowheads.
 * - `arrow-location` (optional): Parameter value at which to place the arrow.
 * - `reverse` (optional): "yes" to reverse arrow direction.
 * - `arrow-width`, `arrow-angles`: Arrowhead geometry overrides.
 * - `stroke` (optional, default "blue"): Stroke color.
 * - `fill` (optional, default "none"): Fill color.
 * - `thickness` (optional, default "2"): Stroke width.
 * - `cliptobbox` (optional, default "yes"): Clip to bounding box.
 * - `outline` (optional): "yes" for outline rendering.
 *
 * @par SVG Output
 * Creates a `<path>` element with M/L commands tracing the curve,
 * clipped to the bounding box and optionally decorated with arrowheads.
 * When `arrow-location` is specified, a separate sub-path of 5 points
 * is appended for proper arrow orientation at that parameter value.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void parametric_curve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
