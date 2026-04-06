#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render an `<image>` XML element as SVG.
 *
 * Embeds a raster or SVG image into the diagram.  The element must contain
 * child elements that provide a tactile fallback — in tactile mode, the
 * image is replaced by rendering those children as a group.
 *
 * @par Image Types
 * - **Raster** (PNG, JPEG, GIF): The file is read, base64-encoded, and
 *   embedded as a data URI in an `<image>` element with a transform for
 *   positioning, scaling, and rotation.
 * - **SVG**: The SVG file is parsed and embedded via `<foreignObject>` with
 *   width/height set to "100%" and a computed viewBox.
 *
 * @par XML Attributes
 * - `source` (required): Path to the image file (relative to assets directory).
 * - `lower-left` (optional, default "(0,0)"): Position of the lower-left corner.
 * - `dimensions` (optional, default "(1,1)"): Width and height in user coordinates.
 * - `center` (optional): Center point (overrides lower-left).
 * - `rotate` (optional, default 0): Rotation angle in degrees.
 * - `scale` (optional, default 1): Scale factor (scalar or 2D vector).
 * - `opacity` (optional): Opacity value (0.0 to 1.0).
 * - `filetype` (optional): Force image type ("png", "jpeg", "gif", "svg").
 *
 * @par SVG Output
 * Creates an `<image>` element (raster) or `<foreignObject>` (SVG) with
 * transform, sizing, and optional opacity.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void image(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
