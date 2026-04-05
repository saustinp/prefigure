#include "prefigure/diffeqs.hpp"

#ifdef PREFIGURE_HAS_DIFFEQS

namespace prefigure {

void de_solve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)element; (void)diagram; (void)parent; (void)status;
}

void plot_de_solution(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)element; (void)diagram; (void)parent; (void)status;
}

}  // namespace prefigure

#endif  // PREFIGURE_HAS_DIFFEQS
