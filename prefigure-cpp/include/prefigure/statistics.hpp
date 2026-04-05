#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<scatter>` XML element as SVG.
 *
 * Plots a collection of data points as individual markers (scatter plot).
 *
 * @par XML Attributes
 * - `data` or `points` (required): Data source expression or coordinate list.
 * - `style` (optional): Marker style ("circle", "box", etc.).
 * - `size` (optional): Marker size.
 * - `stroke`, `fill`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates multiple marker elements (e.g., `<circle>`) at data positions.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void scatter(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<histogram>` XML element as SVG.
 *
 * Draws a histogram from data, dividing the range into bins and drawing
 * rectangles proportional to the frequency or density of each bin.
 *
 * @par XML Attributes
 * - `data` (required): Data source expression.
 * - `bins` (optional): Number of bins.
 * - `fill`, `stroke`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates multiple `<rect>` elements for histogram bars.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void histogram(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
