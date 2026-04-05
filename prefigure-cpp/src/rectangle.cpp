#include "prefigure/rectangle.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>
#include <vector>

namespace prefigure {

static void finish_outline(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           "none",
                           parent);
}

void rectangle(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline(element, diagram, parent);
        return;
    }

    Point2d ll, dims, center_pt;
    bool has_center = false;

    try {
        auto ll_val = diagram.expr_ctx().eval(get_attr(element, "lower-left", "(0,0)"));
        ll = ll_val.as_point();

        auto dims_val = diagram.expr_ctx().eval(get_attr(element, "dimensions", "(1,1)"));
        dims = dims_val.as_point();

        auto center_attr = element.attribute("center");
        if (center_attr) {
            auto center_val = diagram.expr_ctx().eval(center_attr.value());
            center_pt = center_val.as_point();
            ll = center_pt - 0.5 * dims;
            has_center = true;
        } else {
            center_pt = ll + 0.5 * dims;
        }
    } catch (...) {
        spdlog::error("Error parsing data in a <rectangle>");
        return;
    }

    // Create a <path> in scratch space; it will be copied into parent or defs later
    XmlNode path = diagram.get_scratch().append_child("path");
    auto id_attr = element.attribute("id");
    if (id_attr) diagram.add_id(path, id_attr.value());
    diagram.register_svg_element(element, path);

    // Compute rotated corners
    double rotate_deg = 0.0;
    try {
        rotate_deg = diagram.expr_ctx().eval(get_attr(element, "rotate", "0")).to_double();
    } catch (...) {}

    CTM ctm;
    ctm.translate(center_pt[0], center_pt[1]);
    ctm.rotate(rotate_deg);
    double dx = dims[0] / 2.0;
    double dy = dims[1] / 2.0;

    std::vector<Point2d> user_corners = {
        ctm.transform(Point2d(-dx, -dy)),
        ctm.transform(Point2d(dx, -dy)),
        ctm.transform(Point2d(dx, dy)),
        ctm.transform(Point2d(-dx, dy))
    };

    std::vector<Point2d> corners;
    corners.reserve(4);
    for (const auto& c : user_corners) {
        corners.push_back(diagram.transform(c));
    }

    // Parse corner radius
    double radius = 0.0;
    try {
        radius = diagram.expr_ctx().eval(get_attr(element, "corner-radius", "0")).to_double();
    } catch (...) {}

    std::string d;
    if (radius == 0.0) {
        d = "M " + pt2str(corners[0]);
        for (int i = 1; i < 4; ++i) {
            d += " L " + pt2str(corners[i]);
        }
        d += " Z";
    } else {
        // Rounded corners with quadratic Bezier
        std::vector<Point2d> ext_corners = corners;
        ext_corners.push_back(corners[0]);
        ext_corners.push_back(corners[1]);

        std::vector<std::string> cmds;
        for (int i = 0; i < 4; ++i) {
            Eigen::VectorXd v1_vec = ext_corners[i + 1] - ext_corners[i];
            Point2d v1 = normalize(v1_vec).head<2>();
            Eigen::VectorXd v2_vec = ext_corners[i + 2] - ext_corners[i + 1];
            Point2d v2 = normalize(v2_vec).head<2>();

            std::string command = cmds.empty() ? "M" : "L";
            cmds.push_back(command + " " + pt2str(Point2d(ext_corners[i + 1] - radius * v1)));
            cmds.push_back("Q " + pt2str(ext_corners[i + 1]) + " " +
                          pt2str(Point2d(ext_corners[i + 1] + radius * v2)));
        }
        cmds.push_back("Z");
        d = "";
        for (const auto& cmd : cmds) {
            if (!d.empty()) d += " ";
            d += cmd;
        }
    }

    path.append_attribute("d").set_value(d.c_str());

    // Handle tactile vs regular styling
    if (diagram.output_format() == OutputFormat::Tactile) {
        auto stroke_attr = element.attribute("stroke");
        auto fill_attr = element.attribute("fill");
        if (stroke_attr && std::string(stroke_attr.value()) != "none") {
            stroke_attr.set_value("black");
        }
        if (fill_attr) {
            std::string fill_val = fill_attr.value();
            // Trim and lowercase
            std::string lower;
            for (char c : fill_val) {
                if (c != ' ' && c != '\t') lower += std::tolower(c);
            }
            if (lower != "none") {
                fill_attr.set_value("lightgray");
            } else {
                fill_attr.set_value("none");
            }
        }
    } else {
        set_attr(element, "stroke", "none");
        set_attr(element, "fill", "none");
    }

    set_attr(element, "thickness", "2");
    add_attr(path, get_2d_attr(element));
    cliptobbox(path, element, diagram);

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
