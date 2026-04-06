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
 * Supports both scalar slope fields (f returns a number) and system
 * direction fields (f returns a 2D vector [dx, dy]).  Each line segment
 * is normalized to fit within 1/4 of the grid cell spacing, preventing
 * overlap between adjacent segments.  Vertical lines are drawn when the
 * slope is infinite or undefined.
 *
 * @par XML Attributes
 * - `function` (required): A one- or two-argument function.  Returns
 *   a scalar slope (dy/dx) or a 2D vector [dx, dy] for systems.
 * - `system` (optional, default "no"): "yes" if the function returns a vector.
 * - `spacings` (optional): Grid spacing as "(dx, dy)" or full
 *   "(x0, dx, x1, y0, dy, y1)"; auto-computed from bbox if omitted.
 * - `arrows` (optional): "yes" to draw arrowheads on segments.
 * - `stroke` (optional, default "blue"): Segment stroke color.
 * - `thickness` (optional, default "2"): Segment stroke width.
 * - `arrow-width`, `arrow-angles`: Arrowhead geometry overrides.
 * - `outline` (optional): "yes" for outline rendering.
 *
 * @par SVG Output
 * Converts the element to a `<group>` containing `<line>` elements,
 * one per grid point.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void slope_field(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<vector-field>` XML element as SVG.
 *
 * Draws arrows showing a 2D vector field F(x, y) = (u, v).  Operates in
 * two modes:
 *
 * - **Grid mode** (default): Samples the field on a regular grid.
 *   Magnitudes are normalized using an exponent (default 1.0) and scaled
 *   so the longest vector fits within 75% of a grid cell.
 * - **Curve mode** (when `@curve` is specified): Samples the field along
 *   a parametric curve at N evenly spaced parameter values.
 *
 * @par XML Attributes
 * - `function` (required): A function returning a 2D vector at each point.
 * - `spacings` (optional): Grid spacing (same format as slope_field).
 * - `exponent` (optional, default "1"): Magnitude exponent for grid mode.
 *   Values < 1 compress the dynamic range of arrow lengths.
 * - `scale` (optional): Manual scale factor (overrides auto-scaling).
 * - `curve` (optional): Parametric curve function for curve mode.
 * - `domain` (required for curve mode): Parameter range "(t_min, t_max)".
 * - `N` (required for curve mode): Number of sample points along curve.
 * - `stroke` (optional, default "blue"): Arrow stroke color.
 * - `thickness` (optional, default "2"): Arrow stroke width.
 * - `arrow-width`, `arrow-angles`: Arrowhead geometry overrides.
 * - `outline` (optional): "yes" for outline rendering.
 *
 * @par SVG Output
 * Converts the element to a `<group>` containing `<line>` elements
 * with arrowheads.  Arrows shorter than 2 SVG units are suppressed.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void vector_field(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
