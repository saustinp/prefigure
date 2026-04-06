#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render an `<implicit-curve>` XML element as SVG.
 *
 * Traces the level set {(x,y) : f(x,y) = k} using an adaptive QuadTree
 * algorithm with Newton-Raphson edge refinement.
 *
 * @par Algorithm
 * 1. **Initial subdivision**: The bounding box is uniformly subdivided
 *    into a grid of cells to `initial-depth` levels (default 4).
 * 2. **Adaptive refinement**: Each cell that intersects the zero set
 *    (detected by sign changes at corners) is subdivided further,
 *    down to `depth` levels (default 8).
 * 3. **Zero finding**: On leaf cells that intersect, Newton-Raphson
 *    iteration is used along cell edges to locate the curve crossing
 *    to within 1e-6 tolerance (max 50 iterations).
 * 4. **Segment extraction**: Each leaf cell produces one line segment
 *    connecting two edge-crossing points.
 *
 * @par XML Attributes
 * - `function` (required): A two-argument function expression f(x, y).
 * - `value` or `k` (optional, default 0): The level set value.
 * - `depth` (optional, default 8): Maximum QuadTree refinement depth.
 * - `initial-depth` (optional, default 4): Uniform subdivision depth before adaptive refinement.
 * - `stroke` (optional, default "black"): Stroke color.
 * - `thickness` (optional, default "2"): Stroke width.
 * - `outline` (optional): "yes" for outline rendering.
 *
 * @par SVG Output
 * Creates a `<path>` element with M/L segments tracing the implicit curve.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void implicit_curve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
