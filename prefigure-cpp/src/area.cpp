#include "prefigure/area.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>
#include <vector>

namespace prefigure {

static void finish_outline_area(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

void area_between_curves(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_area(element, diagram, parent);
        return;
    }

    bool polar = get_attr(element, "coordinates", "cartesian") == "polar";

    set_attr(element, "stroke", "black");
    set_attr(element, "fill", "lightgray");
    set_attr(element, "thickness", "2");
    if (diagram.output_format() == OutputFormat::Tactile) {
        element.attribute("stroke").set_value("black");
        element.attribute("fill").set_value("lightgray");
    }

    // Retrieve the two functions
    MathFunction f, g;
    auto functions_attr = element.attribute("functions");
    if (functions_attr) {
        try {
            auto val = diagram.expr_ctx().eval(functions_attr.value());
            // This should be a vector of two functions -- but since we can't store
            // a vector of functions in Value, we try function1/function2 attributes
            // or treat as two separate names
            // In practice the Python splits the tuple, so we try evaluating the two halves
            std::string funcs_str = functions_attr.value();
            // Try to parse as "(f, g)" or "[f, g]"
            // Strip parens/brackets
            std::string trimmed = funcs_str;
            if (!trimmed.empty() && (trimmed.front() == '(' || trimmed.front() == '[')) {
                trimmed = trimmed.substr(1);
            }
            if (!trimmed.empty() && (trimmed.back() == ')' || trimmed.back() == ']')) {
                trimmed.pop_back();
            }
            auto comma = trimmed.find(',');
            if (comma != std::string::npos) {
                std::string f_name = trimmed.substr(0, comma);
                std::string g_name = trimmed.substr(comma + 1);
                // Trim whitespace
                auto trim = [](std::string& s) {
                    auto start = s.find_first_not_of(" \t");
                    auto end = s.find_last_not_of(" \t");
                    if (start != std::string::npos) s = s.substr(start, end - start + 1);
                };
                trim(f_name);
                trim(g_name);
                f = diagram.expr_ctx().eval(f_name).as_function();
                g = diagram.expr_ctx().eval(g_name).as_function();
            } else {
                spdlog::error("Error in <area> parsing functions={}", funcs_str);
                return;
            }
        } catch (...) {
            spdlog::error("Error in <area> parsing functions={}", functions_attr.value());
            return;
        }
    } else {
        try {
            f = diagram.expr_ctx().eval(element.attribute("function1").value()).as_function();
        } catch (...) {
            spdlog::error("Error in <area> defining function1={}",
                          get_attr(element, "function1", ""));
            return;
        }
        try {
            g = diagram.expr_ctx().eval(element.attribute("function2").value()).as_function();
        } catch (...) {
            spdlog::error("Error in <area> defining function2={}",
                          get_attr(element, "function2", ""));
            return;
        }
    }

    int N = std::stoi(get_attr(element, "N", "100"));

    // Domain
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

    if (get_attr(element, "domain-degrees", "no") == "yes") {
        domain[0] = domain[0] * M_PI / 180.0;
        domain[1] = domain[1] * M_PI / 180.0;
    }

    double dx = (domain[1] - domain[0]) / N;
    double x = domain[0];

    // Build path: forward trace f, backward trace g
    std::vector<std::string> cmds;

    // First point
    auto eval_point = [&](double xx, const MathFunction& func) -> Point2d {
        if (polar) {
            double r = func(Value(xx)).to_double();
            return diagram.transform(Point2d(r * std::cos(xx), r * std::sin(xx)));
        } else {
            return diagram.transform(Point2d(xx, func(Value(xx)).to_double()));
        }
    };

    try {
        Point2d p = eval_point(x, f);
        cmds.push_back("M " + pt2str(p));
    } catch (...) {
        spdlog::error("Error evaluating area function");
        return;
    }

    // Forward trace f
    for (int i = 0; i <= N; ++i) {
        try {
            Point2d p = eval_point(x, f);
            cmds.push_back("L " + pt2str(p));
        } catch (...) {}
        x += dx;
    }

    // Backward trace g
    for (int i = 0; i <= N; ++i) {
        x -= dx;
        try {
            Point2d p = eval_point(x, g);
            cmds.push_back("L " + pt2str(p));
        } catch (...) {}
    }
    cmds.push_back("Z");

    std::string d;
    for (const auto& c : cmds) {
        if (!d.empty()) d += " ";
        d += c;
    }

    // Create SVG path
    XmlNode path = diagram.get_scratch().append_child("path");
    diagram.add_id(path, get_attr(element, "id", ""));
    diagram.register_svg_element(element, path);

    path.append_attribute("d").set_value(d.c_str());
    add_attr(path, get_2d_attr(element));

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, path, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, path, parent);
        finish_outline_area(element, diagram, parent);
    } else {
        parent.append_copy(path);
    }
}

void area_under_curve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    // Set function1 to the user's function
    std::string func_str = get_attr(element, "function", "none");
    if (!element.attribute("function1")) {
        element.append_attribute("function1").set_value(func_str.c_str());
    } else {
        element.attribute("function1").set_value(func_str.c_str());
    }

    // Define zero function and set as function2
    diagram.expr_ctx().define("__zero(x) = 0");
    if (!element.attribute("function2")) {
        element.append_attribute("function2").set_value("__zero");
    } else {
        element.attribute("function2").set_value("__zero");
    }

    area_between_curves(element, diagram, parent, status);
}

}  // namespace prefigure
