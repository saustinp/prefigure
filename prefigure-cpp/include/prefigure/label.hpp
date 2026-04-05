#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

void label_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void caption(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
bool is_label_tag(const std::string& tag);
std::string evaluate_text(const std::string& text);
void place_labels(Diagram& diagram, const std::string& group_id, XmlNode element);
void init(OutputFormat format, Environment environment);
void add_macros(const std::string& macros);

}  // namespace prefigure
