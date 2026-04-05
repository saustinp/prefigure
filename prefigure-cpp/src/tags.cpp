#include "prefigure/tags.hpp"
#include "prefigure/annotations.hpp"
#include "prefigure/area.hpp"
#include "prefigure/axes.hpp"
#include "prefigure/circle.hpp"
#include "prefigure/clip.hpp"
#include "prefigure/coordinates.hpp"
#include "prefigure/graph.hpp"
#include "prefigure/grid_axes.hpp"
#include "prefigure/group.hpp"
#include "prefigure/image.hpp"
#include "prefigure/implicit.hpp"
#include "prefigure/label.hpp"
#include "prefigure/legend.hpp"
#include "prefigure/line.hpp"
#include "prefigure/parametric_curve.hpp"
#include "prefigure/path_element.hpp"
#include "prefigure/point.hpp"
#include "prefigure/polygon.hpp"
#include "prefigure/read.hpp"
#include "prefigure/rectangle.hpp"
#include "prefigure/repeat.hpp"
#include "prefigure/riemann_sum.hpp"
#include "prefigure/slope_field.hpp"
#include "prefigure/statistics.hpp"
#include "prefigure/tangent_line.hpp"
#include "prefigure/vector_element.hpp"

#ifdef PREFIGURE_HAS_DIFFEQS
#include "prefigure/diffeqs.hpp"
#endif

#ifdef PREFIGURE_HAS_NETWORK
#include "prefigure/network.hpp"
#endif

#ifdef PREFIGURE_HAS_SHAPES
#include "prefigure/shape.hpp"
#endif

namespace prefigure {

const TagDict& get_tag_dict() {
    static const TagDict dict = {
        {"annotations",       annotations},
        {"label",             label_element},
        {"caption",           caption},
        {"legend",            legend_element},
        {"clip",              clip},
        {"group",             group},
        {"repeat",            repeat_element},
        {"read",              read},
        {"image",             image},
        {"graph",             graph},
        {"area-between",      area_between_curves},
        {"area-under",        area_under_curve},
        {"circle",            circle_element},
        {"ellipse",           ellipse},
        {"arc",               arc},
        {"angle-marker",      angle_marker},
        {"line",              line},
        {"point",             point},
        {"polygon",           polygon_element},
        {"spline",            spline},
        {"triangle",          triangle},
        {"rectangle",         rectangle},
        {"path",              path_element},
        {"vector",            vector_element},
        {"parametric-curve",  parametric_curve},
        {"tangent-line",      tangent},
        {"implicit-curve",    implicit_curve},
        {"riemann-sum",       riemann_sum},
        {"slope-field",       slope_field},
        {"vector-field",      vector_field},
        {"scatter",           scatter},
        {"histogram",         histogram},
        {"axes",              axes},
        {"tick-mark",         tick_mark},
        {"grid",              grid},
        {"grid-axes",         grid_axes},
        {"coordinates",       coordinates},

#ifdef PREFIGURE_HAS_DIFFEQS
        {"de-solve",          de_solve},
        {"plot-de-solution",  plot_de_solution},
#endif

#ifdef PREFIGURE_HAS_NETWORK
        {"network",           network},
#endif

#ifdef PREFIGURE_HAS_SHAPES
        {"shape-define",      shape_define},
        {"shape",             shape},
#endif
    };
    return dict;
}

void parse_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    const auto& dict = get_tag_dict();
    std::string tag = element.name();
    auto it = dict.find(tag);
    if (it != dict.end()) {
        it->second(element, diagram, parent, status);
    }
    // Unknown tags are silently ignored
}

}  // namespace prefigure
