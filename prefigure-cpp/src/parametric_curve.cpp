#include "prefigure/parametric_curve.hpp"
#include "prefigure/arrow.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <format>
#include <iterator>
#include <string>
#include <vector>

namespace prefigure {

static void finish_outline(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

void parametric_curve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline(element, diagram, parent);
        return;
    }

    // Retrieve the parametric function
    MathFunction f;
    try {
        auto val = diagram.expr_ctx().eval(element.attribute("function").value());
        if (val.is_function()) {
            f = val.as_function();
        } else {
            spdlog::error("Error in <parametric-curve> defining function={}",
                          element.attribute("function").value());
            return;
        }
    } catch (...) {
        spdlog::error("Error in <parametric-curve> defining function={}",
                      element.attribute("function").value());
        return;
    }

    // Retrieve the domain
    std::array<double, 2> domain;
    try {
        auto dv = diagram.expr_ctx().eval(element.attribute("domain").value());
        auto& v = dv.as_vector();
        domain[0] = v[0];
        domain[1] = v[1];
    } catch (...) {
        spdlog::error("Error in <parametric-curve> defining domain={}",
                      element.attribute("domain").value());
        return;
    }

    int arrows = std::stoi(get_attr(element, "arrows", "0"));
    int N = std::stoi(get_attr(element, "N", "100"));

    double t = domain[0];
    double dt = (domain[1] - domain[0]) / N;

    // Build the SVG path attribute directly into a single reserved string.
    // (Replaced the previous vector<string>+pt2str+join pattern, which
    // allocated ~5 strings per sample point.)
    std::string d;
    d.reserve(static_cast<size_t>(N + 8) * 24);
    auto out = std::back_inserter(d);
    try {
        Point2d p = diagram.transform(f(Value(t)).as_point());
        std::format_to(out, "M {:.1f} {:.1f}", p[0], p[1]);
    } catch (...) {
        spdlog::error("Error evaluating parametric-curve function at t={}", t);
        return;
    }

    for (int i = 0; i < N; ++i) {
        t += dt;
        try {
            Point2d p = diagram.transform(f(Value(t)).as_point());
            std::format_to(out, " L {:.1f} {:.1f}", p[0], p[1]);
        } catch (...) {
            continue;
        }
    }

    if (get_attr(element, "closed", "no") == "yes") {
        d += " Z";
    }

    // Arrow location sub-path
    if (arrows > 0 && element.attribute("arrow-location")) {
        double arrow_location = diagram.expr_ctx().eval(
            element.attribute("arrow-location").value()).to_double();
        int num_pts = 5;
        t = arrow_location - num_pts * dt;
        try {
            Point2d p = diagram.transform(f(Value(t)).as_point());
            std::format_to(out, " M {:.1f} {:.1f}", p[0], p[1]);
        } catch (...) {}
        for (int i = 0; i < num_pts; ++i) {
            t += dt;
            try {
                Point2d p = diagram.transform(f(Value(t)).as_point());
                std::format_to(out, " L {:.1f} {:.1f}", p[0], p[1]);
            } catch (...) {}
        }
    }

    // Set default attributes
    if (diagram.output_format() == OutputFormat::Tactile) {
        element.attribute("stroke") ?
            element.attribute("stroke").set_value("black") :
            element.append_attribute("stroke").set_value("black");
        if (element.attribute("fill")) {
            element.attribute("fill").set_value("lightgray");
        }
    } else {
        set_attr(element, "stroke", "blue");
        set_attr(element, "fill", "none");
    }
    set_attr(element, "thickness", "2");

    // Create SVG path in scratch
    XmlNode path = diagram.get_scratch().append_child("path");
    diagram.add_id(path, get_attr(element, "id", ""));
    diagram.register_svg_element(element, path);

    path.append_attribute("d").set_value(d.c_str());
    add_attr(path, get_2d_attr(element));

    // Clip to bounding box
    if (!element.attribute("cliptobbox")) {
        element.append_attribute("cliptobbox").set_value("yes");
    }
    cliptobbox(path, element, diagram);

    // Arrows
    std::string forward = "marker-end";
    std::string backward = "marker-start";
    if (get_attr(element, "reverse", "no") == "yes") {
        std::swap(forward, backward);
    }

    std::string aw = get_attr(element, "arrow-width", "");
    std::string aa = get_attr(element, "arrow-angles", "");
    if (arrows > 0) {
        add_arrowhead_to_path(diagram, forward, path, aw, aa);
    }
    if (arrows > 1) {
        add_arrowhead_to_path(diagram, backward, path, aw, aa);
    }

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, path, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, path, parent);
        finish_outline(element, diagram, parent);
    } else {
        parent.append_copy(path);
    }
}

}  // namespace prefigure
