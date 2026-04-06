#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<vector>` XML element as SVG.
 *
 * Draws a directed vector (arrow) from a tail point to a computed head,
 * with smart label placement using an 8-quadrant alignment algorithm.
 *
 * The vector direction is specified by `@v`; the head is computed as
 * `tail + scale * v`.  An arrowhead is drawn at the tip, with the shaft
 * shortened to avoid overlap.
 *
 * @par Label Alignment
 * When a label is present, its alignment is automatically chosen based
 * on the vector's angle.  The angle is divided into 8 half-quadrants
 * (45-degree sectors), and each maps to an alignment direction and a
 * perpendicular offset direction so the label doesn't overlap the arrow.
 *
 * @par Namespace Registration
 * The following variables are registered in the expression context:
 * - `v` — the scaled vector
 * - `head` — the head point (tail + v)
 * - `angle` — the vector's angle in radians
 *
 * @par XML Attributes
 * - `v` (required): Direction vector expression, e.g. "(3, 4)".
 * - `tail` (optional, default "(0,0)"): Tail point expression.
 * - `scale` (optional, default "1"): Scalar multiplier for the vector.
 * - `stroke`, `thickness`: Styling (defaults: black, 2).
 * - `label-offset` (optional): Distance to offset label from midpoint.
 * - `label-alignment` (optional): Override automatic alignment.
 *
 * @par SVG Output
 * Creates a `<line>` element with an arrowhead marker at the tip.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see arrow.hpp for arrowhead creation
 */
void vector_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
