#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render an `<image>` XML element as SVG.
 *
 * Embeds a raster image (PNG, JPG, etc.) into the SVG output at a
 * specified position and size within the diagram's coordinate system.
 *
 * @par XML Attributes
 * - `source` (required): Path or URL of the image file.
 * - `lower-left` (optional): Position of the image's lower-left corner.
 * - `dimensions` (optional): Width and height in user coordinates.
 *
 * @par SVG Output
 * Creates an `<image>` element with `href` pointing to the image source.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void image(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
