#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

/**
 * @brief Render a `<path>` XML element as SVG.
 *
 * Builds an SVG path from a sequence of child sub-command elements,
 * starting at a given point and accumulating path data (M, L, C, Q
 * commands) as each sub-element is processed.  The path can include
 * embedded graphical elements (graphs, parametric curves, polygons,
 * splines) whose rendered path data is spliced in.
 *
 * @par XML Attributes
 * - `start` (required): Starting point expression, e.g. "(1, 2)".
 * - `closed` (optional, default "no"): "yes" to close the path with Z.
 * - `stroke`, `fill`, `thickness`: Standard styling (defaults: none, none, 2).
 * - `arrows` (optional): "0", "1", or "2" for arrowheads.
 * - `reverse` (optional): "yes" to swap arrow start/end.
 * - `mid-arrow` (optional): "yes" to add a mid-path arrow marker.
 * - `arrow-width`, `arrow-angles`: Arrowhead geometry overrides.
 * - `cliptobbox` (optional, default "yes"): Clip path to bounding box.
 * - `outline` (optional): "yes" for outline rendering.
 *
 * @par Sub-command Elements
 * The following child elements are recognized inside a `<path>`:
 * - `<moveto>` / `<rmoveto>`: Absolute/relative move (via `point` or `distance`+`heading`).
 * - `<lineto>` / `<rlineto>`: Absolute/relative line (via `point` or `distance`+`heading`).
 * - `<horizontal>` / `<vertical>`: Axis-aligned line by `distance`.
 * - `<cubic-bezier>`: Cubic Bezier via `controls` (flat or nested tuple format).
 * - `<quadratic-bezier>`: Quadratic Bezier via `controls`.
 * - `<arc>`: Circular arc via `center`, `radius`, `range` (angular).
 * - `<repeat>`: Loop variable binding for parameterized path segments.
 * - Graphical tags (`graph`, `parametric-curve`, `polygon`, `spline`):
 *   Rendered internally and their path data spliced into the parent path.
 *
 * @par Decorations
 * A `<lineto>` may carry a `decoration` attribute with semicolon-separated
 * parameters.  Supported decoration types:
 * - `coil` — Coil spring pattern (dimensions, center, number).
 * - `zigzag` — Zigzag pattern (dimensions, center, number).
 * - `wave` — Sine wave pattern (dimensions, center, number).
 * - `ragged` — Random jagged line (offset, step, seed).
 * - `capacitor` — Two parallel plates (dimensions, center).
 *
 * @par SVG Output
 * Creates a `<path>` element with the accumulated `d` attribute,
 * clipped to the bounding box and optionally decorated with arrowheads.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see arrow.hpp for arrowhead creation
 */
void path_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Check whether an XML tag name corresponds to a path sub-command.
 *
 * Returns true for: moveto, rmoveto, lineto, rlineto, horizontal,
 * vertical, cubic-bezier, quadratic-bezier, smooth-cubic, smooth-quadratic.
 *
 * @param tag The tag name to test.
 * @return True if the tag is a recognized path sub-command.
 */
bool is_path_tag(const std::string& tag);

}  // namespace prefigure
