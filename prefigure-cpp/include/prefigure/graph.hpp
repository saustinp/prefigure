#pragma once

#include "types.hpp"

#include <functional>
#include <string>
#include <vector>

namespace prefigure {

void graph(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
