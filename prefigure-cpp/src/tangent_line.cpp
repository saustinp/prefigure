#include "prefigure/tangent_line.hpp"
#include "prefigure/calculus.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/line.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>
#include <vector>

namespace prefigure {

static void finish_outline_tangent(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

void tangent(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_tangent(element, diagram, parent);
        return;
    }

    // Retrieve the function
    MathFunction function;
    try {
        auto val = diagram.expr_ctx().eval(element.attribute("function").value());
        if (val.is_function()) {
            function = val.as_function();
        } else {
            spdlog::error("Error retrieving tangent-line attribute @function={}",
                          element.attribute("function").value());
            return;
        }
    } catch (...) {
        spdlog::error("Error retrieving tangent-line attribute @function={}",
                      element.attribute("function").value());
        return;
    }

    // Retrieve the point
    double a;
    try {
        a = diagram.expr_ctx().eval(element.attribute("point").value()).to_double();
    } catch (...) {
        spdlog::error("Error parsing tangent-line attribute @point={}",
                      element.attribute("point").value());
        return;
    }

    // Compute derivative and tangent line function
    double y0 = function(Value(a)).to_double();
    auto scalar_f = [&function](double x) -> double {
        return function(Value(x)).to_double();
    };
    double m = derivative(scalar_f, a);

    auto tangent_func = [y0, m, a](double x) -> double {
        return y0 + m * (x - a);
    };

    // Enter named tangent function into namespace if requested
    auto name_attr = element.attribute("name");
    if (name_attr) {
        MathFunction named_tangent = [tangent_func](Value v) -> Value {
            return Value(tangent_func(v.to_double()));
        };
        diagram.expr_ctx().enter_function(name_attr.value(), named_tangent);
    }

    // Determine the domain
    BBox bbox = diagram.bbox();
    std::array<double, 2> domain;
    auto domain_attr = element.attribute("domain");
    if (!domain_attr) {
        domain = {bbox[0], bbox[2]};
    } else {
        auto dv = diagram.expr_ctx().eval(domain_attr.value());
        auto& v = dv.as_vector();
        domain[0] = v[0];
        domain[1] = v[1];
    }

    auto scales = diagram.get_scales();
    double x1 = domain[0];
    double x2 = domain[1];

    XmlNode line_el;

    if (scales[0] == "linear" && scales[1] == "linear") {
        double y1 = tangent_func(x1);
        double y2 = tangent_func(x2);
        Point2d p1(x1, y1);
        Point2d p2(x2, y2);

        if (get_attr(element, "infinite", "no") == "yes" || !domain_attr) {
            auto result = infinite_line(p1, p2, diagram);
            if (!result) return;
            p1 = result->first;
            p2 = result->second;
        }

        line_el = mk_line(p1, p2, diagram, get_attr(element, "id", ""));
    } else {
        line_el = diagram.get_scratch().append_child("path");

        // Generate positions
        int num_pts = 101;
        std::vector<double> x_positions(num_pts);
        if (scales[0] == "log") {
            double log_start = std::log10(x1);
            double log_end = std::log10(x2);
            for (int i = 0; i < num_pts; ++i) {
                double t = log_start + (log_end - log_start) * i / (num_pts - 1);
                x_positions[i] = std::pow(10.0, t);
            }
        } else {
            for (int i = 0; i < num_pts; ++i) {
                x_positions[i] = x1 + (x2 - x1) * i / (num_pts - 1);
            }
        }

        std::vector<std::string> cmds;
        std::string next_cmd = "M";
        for (double x : x_positions) {
            double y = tangent_func(x);
            if (y < 0 && scales[1] == "log") {
                next_cmd = "M";
                continue;
            }
            cmds.push_back(next_cmd);
            cmds.push_back(pt2str(diagram.transform(Point2d(x, y))));
            next_cmd = "L";
        }

        std::string d;
        for (const auto& c : cmds) {
            if (!d.empty()) d += " ";
            d += c;
        }
        line_el.append_attribute("d").set_value(d.c_str());
    }

    diagram.register_svg_element(element, line_el);

    if (diagram.output_format() == OutputFormat::Tactile) {
        element.attribute("stroke") ?
            element.attribute("stroke").set_value("black") :
            element.append_attribute("stroke").set_value("black");
    } else {
        set_attr(element, "stroke", "red");
    }
    set_attr(element, "thickness", "2");

    add_attr(line_el, get_1d_attr(element));

    // Force clip to bbox
    if (!element.attribute("cliptobbox")) {
        element.append_attribute("cliptobbox").set_value("yes");
    }
    cliptobbox(line_el, element, diagram);

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, line_el, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, line_el, parent);
        finish_outline_tangent(element, diagram, parent);
    } else {
        parent.append_copy(line_el);
    }
}

}  // namespace prefigure
