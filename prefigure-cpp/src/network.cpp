#include "prefigure/network.hpp"

#ifdef PREFIGURE_HAS_NETWORK

namespace prefigure {

void network(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)element; (void)diagram; (void)parent; (void)status;
}

}  // namespace prefigure

#endif  // PREFIGURE_HAS_NETWORK
