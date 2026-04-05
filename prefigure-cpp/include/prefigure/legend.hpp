#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<legend>` XML element as SVG.
 *
 * Draws a legend box listing labeled entries for curves, regions, and
 * other diagram elements, with color swatches or line samples.
 *
 * @par XML Attributes
 * - `position` (optional): Location of the legend box.
 * - Child `<entry>` elements define individual legend items.
 *
 * @par SVG Output
 * Creates a `<g>` element containing rectangles and text for each legend entry.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void legend_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
