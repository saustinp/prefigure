#pragma once

#include "types.hpp"

#ifdef PREFIGURE_HAS_SHAPES

namespace prefigure {

/**
 * @brief Render a `<shape-define>` XML element by registering a named shape.
 *
 * Creates an SVG path from the element's attributes and registers it in
 * the diagram's shape dictionary for later use by `<clip>` and `<shape>`
 * elements.
 *
 * @par XML Attributes
 * - `id` (required): Unique identifier for the shape.
 * - Shape definition attributes (implementation-specific).
 *
 * @par SVG Output
 * Adds a `<path>` to `<defs>` and the shape dictionary; no visible output.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node (unused for definition-only output).
 * @param status  Outline rendering pass.
 *
 * @see Diagram::add_shape()
 */
void shape_define(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<shape>` XML element as SVG.
 *
 * Draws a previously defined shape at a specified position, or uses the
 * shape as a `<use>` reference.
 *
 * @par XML Attributes
 * - `shape` (required): ID of a previously defined shape.
 * - `at` (optional): Position to place the shape.
 * - `stroke`, `fill`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<use>` element referencing the shape definition in `<defs>`.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see Diagram::get_shape()
 */
void shape(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure

#endif  // PREFIGURE_HAS_SHAPES
