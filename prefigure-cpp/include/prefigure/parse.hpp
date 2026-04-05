#pragma once

#include "types.hpp"

#include <optional>
#include <string>

namespace prefigure {

Diagram mk_diagram(const std::string& source,
                   const std::string& filename = "",
                   std::optional<int> diagram_number = std::nullopt,
                   OutputFormat format = OutputFormat::SVG,
                   std::optional<std::string> output = std::nullopt,
                   XmlNode publication = XmlNode(),
                   bool suppress_caption = false,
                   Environment environment = Environment::Pretext);

void parse(const std::string& source,
           const std::string& filename = "",
           std::optional<int> diagram_number = std::nullopt,
           OutputFormat format = OutputFormat::SVG,
           std::optional<std::string> output = std::nullopt,
           XmlNode publication = XmlNode(),
           bool suppress_caption = false,
           Environment environment = Environment::Pretext);

}  // namespace prefigure
