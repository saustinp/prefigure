#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<clip>` XML element by clipping child content to a named shape.
 *
 * Looks up a previously defined `<shape>` by its `@shape` attribute, wraps
 * it in an SVG `<clipPath>` element registered in `<defs>`, and creates a
 * `<g>` with `clip-path` set to that clip path.  Child elements are then
 * parsed inside the clipped group.
 *
 * @par XML Attributes
 * - `shape` (required): ID of a previously defined shape to clip to.
 *
 * @par SVG Output
 * Creates a `<clipPath>` in `<defs>` and a `<g clip-path="url(#...)">` containing children.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see Diagram::recall_shape(), Diagram::add_reusable()
 */
void clip(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
