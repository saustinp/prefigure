#pragma once

#include "types.hpp"

namespace prefigure {

void circle_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void ellipse(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void arc(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void angle_marker(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
