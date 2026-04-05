#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<group>` XML element as SVG.
 *
 * Creates an SVG `<g>` element that groups child content together.
 * Supports optional SVG transforms (translate, rotate, scale, matrix)
 * and two-pass outline rendering for tactile output.
 *
 * @par XML Attributes
 * - `transform` (optional): SVG transform string applied to the group.
 * - `outline` (optional): Outline mode -- "always", "svg", or "tactile"
 *   to enable two-pass outline rendering for this group.
 * - Standard styling attributes are inherited by children.
 *
 * @par SVG Output
 * Creates one `<g>` element (normal mode) or two `<g>` elements (outline mode:
 * one for white background strokes, one for colored foreground strokes).
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see transform_group(), transform_translate(), transform_rotate(), transform_scale()
 */
void group(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
