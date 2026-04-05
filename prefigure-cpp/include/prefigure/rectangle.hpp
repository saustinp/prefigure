#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<rectangle>` XML element as SVG.
 *
 * Draws an axis-aligned or rotated rectangle.  The rectangle can be
 * specified by its lower-left corner and dimensions, or by its center
 * and dimensions.
 *
 * @par XML Attributes
 * - `lower-left` (optional, default: "(0,0)"): Bottom-left corner in user coords.
 * - `dimensions` (optional, default: "(1,1)"): Width and height in user coords.
 * - `center` (optional): If specified, overrides `lower-left` to center the rectangle here.
 * - `rotate` (optional): Rotation angle in degrees about the center.
 * - `stroke`, `fill`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` element (not `<rect>`) to support rotation and shape operations.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void rectangle(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
