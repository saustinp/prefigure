#include "prefigure/tags.hpp"
#include "prefigure/annotations.hpp"
#include "prefigure/area.hpp"
#include "prefigure/axes.hpp"
#include "prefigure/circle.hpp"
#include "prefigure/clip.hpp"
#include "prefigure/coordinates.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/definition.hpp"
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

#include <spdlog/spdlog.h>

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
namespace tags {

// Helper to wrap a function pointer into ElementHandler
static ElementHandler wrap(void(*fn)(XmlNode, Diagram&, XmlNode, OutlineStatus)) {
    return fn;
}

const TagDict& get_tag_dict() {
    static const TagDict dict = {
        {"angle-marker",      wrap(angle_marker)},
        {"annotations",       wrap(annotations)},
        {"arc",               wrap(arc)},
        {"area-between",      wrap(area_between_curves)},
        {"area-between-curves", wrap(area_between_curves)},
        {"area-under",        wrap(area_under_curve)},
        {"area-under-curve",  wrap(area_under_curve)},
        {"axes",              wrap(axes)},
        {"caption",           wrap(caption)},
        {"circle",            wrap(circle_element)},
        {"clip",              wrap(clip)},
        {"contour",           wrap(implicit_curve)},
        {"coordinates",       wrap(coordinates)},
        {"definition",        wrap(definition)},
        {"derivative",        wrap(definition_derivative)},
        {"ellipse",           wrap(ellipse)},
        {"graph",             wrap(graph)},
        {"grid",              wrap(grid)},
        {"grid-axes",         wrap(grid_axes)},
        {"group",             wrap(group)},
        {"histogram",         wrap(histogram)},
        {"image",             wrap(image)},
        {"implicit-curve",    wrap(implicit_curve)},
        {"label",             wrap(label_element)},
        {"legend",            wrap(legend_element)},
        {"line",              wrap(line)},
        {"parametric-curve",  wrap(parametric_curve)},
        {"path",              wrap(path_element)},
        {"point",             wrap(point)},
        {"polygon",           [](XmlNode e, Diagram& d, XmlNode p, OutlineStatus s) { polygon_element(e, d, p, s); }},
        {"read",              wrap(read)},
        {"rectangle",         wrap(rectangle)},
        {"repeat",            wrap(repeat_element)},
        {"riemann-sum",       wrap(riemann_sum)},
        {"rotate",            wrap(transform_rotate)},
        {"scale",             wrap(transform_scale)},
        {"scatter",           wrap(scatter)},
        {"slope-field",       wrap(slope_field)},
        {"spline",            wrap(spline)},
        {"tangent-line",      wrap(tangent)},
        {"tick-mark",         wrap(tick_mark)},
        {"transform",         wrap(transform_group)},
        {"translate",         wrap(transform_translate)},
        {"triangle",          wrap(triangle)},
        {"vector",            wrap(vector_element)},
        {"vector-field",      wrap(vector_field)},

#ifdef PREFIGURE_HAS_DIFFEQS
        {"de-solve",          wrap(de_solve)},
        {"plot-de-solution",  wrap(plot_de_solution)},
#endif

#ifdef PREFIGURE_HAS_NETWORK
        {"network",           wrap(network)},
#endif

#ifdef PREFIGURE_HAS_SHAPES
        {"define-shapes",     wrap(shape_define)},
        {"shape",             wrap(shape)},
#endif
    };
    return dict;
}

void parse_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    // Skip comments
    if (element.type() == pugi::node_comment) {
        return;
    }

    std::string tag = element.name();

    // Check for misplaced tags
    if (is_path_tag(tag)) {
        spdlog::warn("A <{}> tag can only occur inside a <path>", tag);
        return;
    }
    if (is_label_tag(tag)) {
        spdlog::warn("A <{}> tag can only occur inside a <label>", tag);
        return;
    }
    if (is_axes_tag(tag)) {
        spdlog::warn("A <{}> tag can only occur inside a <axes> or <grid-axes>", tag);
        return;
    }

    const auto& dict = get_tag_dict();
    auto it = dict.find(tag);
    if (it == dict.end()) {
        spdlog::error("Unknown element tag: {}", tag);
        return;
    }

    // Debug logging
    if (tag == "definition") {
        auto text = element.child_value();
        if (!text || std::string(text).empty()) {
            spdlog::error("PreFigure is ignoring an empty definition");
            return;
        }
        std::string trimmed = text;
        auto start = trimmed.find_first_not_of(" \t\n\r");
        auto end = trimmed.find_last_not_of(" \t\n\r");
        if (start != std::string::npos) {
            trimmed = trimmed.substr(start, end - start + 1);
        }
        spdlog::debug("Processing definition: {}", trimmed);
    } else {
        std::string msg = "Processing element " + tag;
        auto at = element.attribute("at");
        if (at) {
            msg += " with handle ";
            msg += at.value();
        }
        spdlog::debug("{}", msg);
    }

    it->second(element, diagram, parent, status);
}

}  // namespace tags
}  // namespace prefigure
