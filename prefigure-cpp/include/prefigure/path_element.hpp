#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

/**
 * @brief Render a `<path>` XML element as SVG.
 *
 * Processes an SVG path definition specified in the element and produces
 * an SVG `<path>` element.
 *
 * @par XML Attributes
 * - `d` (required): SVG path data string.
 * - `stroke`, `fill`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` element with the specified path data.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void path_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Check whether an XML tag name corresponds to a path-related element.
 *
 * @param tag The tag name to test.
 * @return True if the tag is a path element tag.
 *
 * @note Currently a stub that always returns false.
 */
bool is_path_tag(const std::string& tag);

}  // namespace prefigure
