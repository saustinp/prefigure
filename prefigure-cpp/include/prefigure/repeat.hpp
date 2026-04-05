#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

/**
 * @brief Render a `<repeat>` XML element by iterating its children over a parameter range.
 *
 * Evaluates child elements multiple times, binding a loop variable to each
 * value in a specified range or list.  Each iteration appends an ID suffix
 * to ensure unique SVG element IDs.
 *
 * @par XML Attributes
 * - `parameter` (required): Loop specification, e.g., "k=0..5" or "k in [1,2,3]".
 *
 * @par SVG Output
 * Produces repeated copies of the child SVG content, one per iteration.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see Diagram::push_id_suffix(), Diagram::pop_id_suffix()
 */
void repeat_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Replace characters disallowed by EPUB in an ID string.
 *
 * Maps parentheses, brackets, braces, commas, periods, equals, and hash
 * to EPUB-safe substitutes.  Unknown characters are replaced with '_'.
 *
 * @param text The input string (typically an element ID).
 * @return An EPUB-compliant version of the string.
 */
std::string epub_clean(const std::string& text);

}  // namespace prefigure
