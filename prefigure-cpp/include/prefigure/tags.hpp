#pragma once

#include "types.hpp"

#include <map>
#include <string>

namespace prefigure {

/**
 * @brief Tag dispatch registry and element parsing.
 *
 * The tags namespace maintains a global dictionary mapping XML tag names
 * (e.g., "line", "circle", "graph") to their ElementHandler functions.
 * During Diagram::parse(), each child element is dispatched through
 * parse_element() which looks up the handler and invokes it.
 */
namespace tags {

/**
 * @brief Type alias for the tag-to-handler dictionary.
 *
 * Maps XML tag name strings to ElementHandler callables.
 */
using TagDict = std::map<std::string, ElementHandler>;

/**
 * @brief Get the global tag dictionary (read-only).
 *
 * The dictionary is lazily initialized on first call and contains
 * entries for all recognized PreFigure XML element tags.
 *
 * @return A const reference to the tag dictionary.
 */
const TagDict& get_tag_dict();

/**
 * @brief Dispatch an XML element to its registered handler.
 *
 * Looks up the element's tag name in the tag dictionary and invokes
 * the corresponding ElementHandler.  If no handler is registered for
 * the tag, the element is silently skipped (or an error is logged,
 * depending on the tag).
 *
 * @param element The XML element to process.
 * @param diagram The parent Diagram context.
 * @param parent  The SVG parent node for appending output.
 * @param status  The current outline rendering pass.
 *
 * @see ElementHandler, Diagram::parse()
 */
void parse_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace tags
}  // namespace prefigure
