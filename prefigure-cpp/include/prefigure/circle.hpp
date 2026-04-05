#pragma once

#include "types.hpp"

#include <string>
#include <vector>

namespace prefigure {

// Core path generation for circles/ellipses/arcs.
// center, axes_length, angular_range(degrees), rotate(degrees), N samples.
// Returns SVG path commands (M ... L ... L ...).
std::vector<std::string> make_circle_path(Diagram& diagram,
                                          const Point2d& center,
                                          const Point2d& axes_length,
                                          const Point2d& angular_range,
                                          double rotate = 0.0,
                                          int N = 100);

void circle_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void ellipse(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void arc(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);
void angle_marker(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
