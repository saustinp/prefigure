#pragma once

#include "types.hpp"

#ifdef PREFIGURE_HAS_NETWORK

namespace prefigure {

void network(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure

#endif  // PREFIGURE_HAS_NETWORK
