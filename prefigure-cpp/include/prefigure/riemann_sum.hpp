#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<riemann-sum>` XML element as SVG.
 *
 * Partitions a domain into N subintervals and renders each as an
 * area-under-curve element, approximating the integral according to
 * the chosen rule.  The element is transformed into a `<group>` containing
 * N `<area-under-curve>` children, each with a constant or fitted function.
 *
 * @par Approximation Rules
 * - `left` (default): Sample at left endpoint of each subinterval.
 * - `right`: Sample at right endpoint.
 * - `midpoint`: Sample at midpoint.
 * - `user-defined`: Use explicit sample points from `@samples`.
 * - `upper`: Maximum function value over 101 points in each subinterval.
 * - `lower`: Minimum function value over 101 points in each subinterval.
 * - `trapezoidal`: Linear interpolation between endpoints (N=1 per sub-element).
 * - `simpsons`: Parabola fitted through (left, mid, right) using a 2x2 system
 *   to solve for coefficients a, b in a(x-mid)^2 + b(x-mid) + c.
 *
 * @par XML Attributes
 * - `function` (required): Function name to approximate.
 * - `domain` (optional): Evaluation interval "(a, b)"; defaults to bbox x-range.
 * - `N` (required unless `partition` given): Number of subintervals.
 * - `partition` (optional): Explicit partition points as a vector.
 * - `samples` (optional): Explicit sample points (implies "user-defined" rule).
 * - `rule` (optional): Approximation rule name (see above).
 * - `stroke`, `fill`, `thickness`, `miterlimit`: Standard styling.
 * - `annotate` (optional): "yes" to generate accessibility annotations.
 * - `subinterval-text` (optional): Template for per-interval annotation text.
 *
 * @par Namespace Variables
 * For each subinterval, the following are available in annotation templates:
 * - `_interval` — current interval index (0-based)
 * - `_left`, `_right` — interval endpoint strings
 * - `_height` — function value at the sample point (constant rules only)
 *
 * @par SVG Output
 * Creates a group of N `<area-under-curve>` elements, each rendering one
 * rectangle or fitted region.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see area_under_curve()
 */
void riemann_sum(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
