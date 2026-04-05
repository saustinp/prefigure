#pragma once

#include "types.hpp"

#include <optional>
#include <set>
#include <string>

namespace prefigure {

// Create a diagram from an XML element and process it through all phases
void mk_diagram(XmlNode element,
                OutputFormat format,
                XmlNode publication,
                const std::string& filename,
                bool suppress_caption,
                std::optional<int> diagram_number,
                Environment environment,
                bool return_string = false);

// Parse a PreFigure XML file, find <diagram> elements, and build each one
void parse(const std::string& filename,
           OutputFormat format = OutputFormat::SVG,
           const std::string& pub_file = "",
           bool suppress_caption = false,
           Environment environment = Environment::Pretext);

// Check for duplicate handles in the element tree
void check_duplicate_handles(XmlNode element, std::set<std::string>& handles);

}  // namespace prefigure
