#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render an `<axes>` XML element as SVG.
 *
 * Draws x and y coordinate axes through the origin (or a specified center)
 * with tick marks and optional labels.
 *
 * @par XML Attributes
 * - `xlabel`, `ylabel` (optional): Axis label text.
 * - `decorations` (optional, default: "yes"): Whether to draw tick marks.
 * - `hticks`, `vticks` (optional): Custom tick mark positions.
 * - `arrows` (optional): Arrowhead style on axis ends.
 * - `stroke`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates `<line>` elements for the axes and `<g>` groups for tick marks.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<tick-mark>` XML element as SVG.
 *
 * Draws a single tick mark at a specified position on an axis.
 *
 * @par XML Attributes
 * - `at` (required): Position along the axis.
 * - `axis` (optional): Which axis ("x" or "y").
 * - `label` (optional): Tick label text.
 *
 * @par SVG Output
 * Creates a short `<line>` element perpendicular to the axis.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void tick_mark(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
