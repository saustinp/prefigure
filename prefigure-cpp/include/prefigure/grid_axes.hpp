#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

/**
 * @brief Render a `<grid>` XML element as SVG.
 *
 * Draws a background grid of horizontal and vertical lines at regular
 * intervals within the current bounding box.
 *
 * @par XML Attributes
 * - `spacings` (optional): Grid spacing "(dx, dy)".
 * - `stroke` (optional, default: "lightgray"): Grid line color.
 * - `thickness` (optional): Grid line width.
 *
 * @par SVG Output
 * Creates multiple `<line>` elements forming a rectangular grid.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void grid(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<grid-axes>` XML element as SVG.
 *
 * Combines a background grid with labeled axes, providing a complete
 * coordinate reference frame.  Equivalent to a `<grid>` followed by
 * an `<axes>` with matching parameters.
 *
 * @par XML Attributes
 * - `spacings` (optional): Grid spacing "(dx, dy)".
 * - `xlabel`, `ylabel` (optional): Axis label text.
 * - Accepts all attributes from both `<grid>` and `<axes>`.
 *
 * @par SVG Output
 * Creates grid lines, axis lines, tick marks, and axis labels.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void grid_axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Check whether an XML tag name is an axes-related element.
 *
 * Returns true for tags like "axes", "grid", "grid-axes", "tick-mark", etc.
 *
 * @param tag The tag name to test.
 * @return True if the tag is an axes-related element.
 */
bool is_axes_tag(const std::string& tag);

}  // namespace prefigure
