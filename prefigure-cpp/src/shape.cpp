#include "prefigure/shape.hpp"

#ifdef PREFIGURE_HAS_SHAPES

namespace prefigure {

void shape_define(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)element; (void)diagram; (void)parent; (void)status;
}

void shape(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)element; (void)diagram; (void)parent; (void)status;
}

}  // namespace prefigure

#endif  // PREFIGURE_HAS_SHAPES
