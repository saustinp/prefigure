#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<coordinates>` XML element by establishing a new coordinate system.
 *
 * Pushes a new CTM and bounding box onto the diagram's transform stack,
 * mapping a user-specified bounding box into a destination rectangle
 * within the current coordinate system.  Child elements are then parsed
 * in this new coordinate space.
 *
 * @par XML Attributes
 * - `bbox` (required): The new bounding box in user coordinates, e.g., "(-5,-5,5,5)".
 * - `destination` (optional): Target rectangle in the parent coordinate system;
 *   defaults to the current bbox.
 * - `aspect-ratio` (optional): Force a specific aspect ratio for the new coordinate system.
 * - `preserve-y-range` (optional, default: "no"): If "yes", keep y-range fixed when
 *   adjusting for aspect ratio.
 * - `scale` (optional): Axis scale types, e.g., "log-linear" for log x-axis.
 *
 * @par SVG Output
 * Creates a `<g>` element containing all child output, with an associated clip path.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see CTM, Diagram::push_ctm(), Diagram::pop_ctm()
 */
void coordinates(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
