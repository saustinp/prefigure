#pragma once

#include "types.hpp"

#include <map>
#include <string>

namespace prefigure {
namespace tags {

// Tag registry: maps XML tag names to handler functions
using TagDict = std::map<std::string, ElementHandler>;

// Get the global tag dictionary
const TagDict& get_tag_dict();

// Parse a single element by looking up its tag in the registry
void parse_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace tags
}  // namespace prefigure
