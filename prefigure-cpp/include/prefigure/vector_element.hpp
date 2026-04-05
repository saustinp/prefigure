#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<vector>` XML element as SVG.
 *
 * Draws a directed vector (arrow) from a tail point to a head point,
 * or from a tail with a given direction and magnitude.
 *
 * @par XML Attributes
 * - `tail` (required): Starting point expression.
 * - `head` or `vector` (required): Ending point or direction vector expression.
 * - `arrows` (optional): Arrowhead configuration.
 * - `stroke`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<line>` or `<path>` element with arrowhead markers.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void vector_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
