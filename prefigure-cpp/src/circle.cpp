#include "prefigure/circle.hpp"
#include "prefigure/arrow.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/label.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <string>
#include <unordered_map>
#include <vector>

namespace prefigure {

// Alignment helpers (duplicated from point.cpp -- ideally shared)
// Forward declarations
static void finish_outline_circle(XmlNode element, Diagram& diagram, XmlNode parent);
static XmlNode add_label_circle(XmlNode element, Diagram& diagram, XmlNode parent);

std::vector<std::string> make_circle_path(Diagram& diagram,
                                          const Point2d& center,
                                          const Point2d& axes_length,
                                          const Point2d& angular_range,
                                          double rotate_deg,
                                          int N) {
    CTM ctm;
    ctm.translate(center[0], center[1]);
    ctm.rotate(rotate_deg);
    ctm.scale(axes_length[0], axes_length[1]);

    double start_rad = angular_range[0] * M_PI / 180.0;
    double end_rad = angular_range[1] * M_PI / 180.0;
    double dt = (end_rad - start_rad) / N;
    double t = start_rad;

    std::vector<std::string> cmds;
    cmds.reserve(2 * N + 2);
    for (int i = 0; i < N; ++i) {
        Point2d pt = ctm.transform(Point2d(std::cos(t), std::sin(t)));
        pt = diagram.transform(pt);
        std::string command = cmds.empty() ? "M" : "L";
        cmds.push_back(command);
        cmds.push_back(pt2str(pt));
        t += dt;
    }
    return cmds;
}

void circle_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_circle(element, diagram, parent);
        return;
    }

    Point2d center;
    try {
        center = diagram.expr_ctx().eval(element.attribute("center").value()).as_point();
    } catch (...) {
        spdlog::error("Error in <circle> parsing center={}", element.attribute("center").value());
        return;
    }

    double radius = 1.0;
    try {
        radius = diagram.expr_ctx().eval(get_attr(element, "radius", "1")).to_double();
    } catch (...) {
        spdlog::error("Error in <circle> parsing radius={}", element.attribute("radius").value());
        return;
    }

    XmlNode circle = diagram.get_scratch().append_child("path");
    auto id_attr = element.attribute("id");
    if (id_attr) diagram.add_id(circle, id_attr.value());
    diagram.register_svg_element(element, circle);

    int N = 100;
    try { N = static_cast<int>(diagram.expr_ctx().eval(get_attr(element, "N", "100")).to_double()); } catch (...) {}

    auto cmds = make_circle_path(diagram, center, Point2d(radius, radius), Point2d(0, 360), 0.0, N);
    cmds.push_back("Z");

    std::string d;
    for (const auto& c : cmds) {
        if (!d.empty()) d += " ";
        d += c;
    }
    circle.append_attribute("d").set_value(d.c_str());

    // Styling
    if (diagram.output_format() == OutputFormat::Tactile) {
        if (element.attribute("stroke"))
            element.attribute("stroke").set_value("black");
        auto fill_attr = element.attribute("fill");
        if (fill_attr) {
            std::string fv = fill_attr.value();
            std::string lower;
            for (char c : fv) if (c != ' ') lower += std::tolower(c);
            if (lower != "none") fill_attr.set_value("lightgray");
            else fill_attr.set_value("none");
        }
    } else {
        set_attr(element, "stroke", "black");
        set_attr(element, "fill", "none");
    }
    set_attr(element, "thickness", "2");
    add_attr(circle, get_2d_attr(element));
    cliptobbox(circle, element, diagram);

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, circle, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, circle, parent);
        finish_outline_circle(element, diagram, parent);
    } else {
        parent.append_copy(circle);
    }
}

static void finish_outline_circle(XmlNode element, Diagram& diagram, XmlNode parent) {
    XmlNode original_parent = parent;
    parent = add_label_circle(element, diagram, parent);

    if (original_parent != parent) {
        for (auto child = parent.first_child(); child; child = child.next_sibling()) {
            if (child.attribute("id")) child.remove_attribute("id");
        }
    }

    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           element.attribute("fill").value(),
                           parent);
}

void ellipse(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_circle(element, diagram, parent);
        return;
    }

    Point2d center;
    try {
        center = diagram.expr_ctx().eval(element.attribute("center").value()).as_point();
    } catch (...) {
        spdlog::error("Error in <ellipse> parsing center={}", element.attribute("center").value());
        return;
    }

    Point2d axes(1.0, 1.0);
    try {
        axes = diagram.expr_ctx().eval(get_attr(element, "axes", "(1,1)")).as_point();
    } catch (...) {
        spdlog::error("Error in <ellipse> parsing axes={}", element.attribute("axes").value());
        return;
    }

    double rotate_deg = 0.0;
    try {
        rotate_deg = diagram.expr_ctx().eval(get_attr(element, "rotate", "0")).to_double();
    } catch (...) {}
    if (get_attr(element, "degrees", "yes") == "no") {
        rotate_deg = rotate_deg * 180.0 / M_PI;
    }

    int N = 100;
    try { N = static_cast<int>(diagram.expr_ctx().eval(get_attr(element, "N", "100")).to_double()); } catch (...) {}

    XmlNode ell = diagram.get_scratch().append_child("path");
    auto id_attr = element.attribute("id");
    if (id_attr) diagram.add_id(ell, id_attr.value());
    diagram.register_svg_element(element, ell);

    auto cmds = make_circle_path(diagram, center, axes, Point2d(0, 360), rotate_deg, N);
    cmds.push_back("Z");

    std::string d;
    for (const auto& c : cmds) {
        if (!d.empty()) d += " ";
        d += c;
    }
    ell.append_attribute("d").set_value(d.c_str());

    // Styling
    if (diagram.output_format() == OutputFormat::Tactile) {
        if (element.attribute("stroke"))
            element.attribute("stroke").set_value("black");
        auto fill_attr = element.attribute("fill");
        if (fill_attr) {
            std::string fv = fill_attr.value();
            std::string lower;
            for (char c : fv) if (c != ' ') lower += std::tolower(c);
            if (lower != "none") fill_attr.set_value("lightgray");
            else fill_attr.set_value("none");
        }
    } else {
        set_attr(element, "stroke", "none");
        set_attr(element, "fill", "none");
    }
    set_attr(element, "thickness", "2");
    add_attr(ell, get_2d_attr(element));
    cliptobbox(ell, element, diagram);

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, ell, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, ell, parent);
        finish_outline_circle(element, diagram, parent);
    } else {
        parent.append_copy(ell);
    }
}

void arc(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_circle(element, diagram, parent);
        return;
    }

    // Styling up front (like Python)
    if (diagram.output_format() == OutputFormat::Tactile) {
        if (element.attribute("stroke"))
            element.attribute("stroke").set_value("black");
        std::string fill = get_attr(element, "fill", "none");
        if (fill != "none") set_attr(element, "fill", "lightgray");
    } else {
        set_attr(element, "stroke", "none");
        set_attr(element, "fill", "none");
    }
    set_attr(element, "thickness", "2");

    Point2d center;
    Point2d angular_range;

    auto points_attr = element.attribute("points");
    if (points_attr) {
        try {
            auto pts_val = diagram.expr_ctx().eval(points_attr.value());
            auto& v = pts_val.as_vector();
            // Expect 6 values: p0x, p0y, p1x, p1y, p2x, p2y
            Point2d pt0(v[0], v[1]);
            Point2d pt1(v[2], v[3]);
            Point2d pt2(v[4], v[5]);

            center = pt1;
            Point2d va = pt0 - pt1;
            Point2d vb = pt2 - pt1;
            double start = std::atan2(va[1], va[0]) * 180.0 / M_PI;
            double stop = std::atan2(vb[1], vb[0]) * 180.0 / M_PI;
            if (stop < start) stop += 360.0;
            angular_range = Point2d(start, stop);
            if (element.attribute("degrees")) {
                element.attribute("degrees").set_value("yes");
            } else {
                element.append_attribute("degrees").set_value("yes");
            }
        } catch (...) {
            spdlog::error("Error in <arc> parsing points={}", points_attr.value());
            return;
        }
    } else {
        try {
            center = diagram.expr_ctx().eval(element.attribute("center").value()).as_point();
        } catch (...) {
            spdlog::error("Error in <arc> parsing center={}", element.attribute("center").value());
            return;
        }
        try {
            angular_range = diagram.expr_ctx().eval(element.attribute("range").value()).as_point();
        } catch (...) {
            spdlog::error("Error in <arc> parsing range={}", element.attribute("range").value());
            return;
        }
    }

    double radius = 1.0;
    try {
        radius = diagram.expr_ctx().eval(element.attribute("radius").value()).to_double();
    } catch (...) {
        spdlog::error("Error in <arc> parsing radius={}", element.attribute("radius").value());
        return;
    }

    bool sector = get_attr(element, "sector", "no") == "yes";

    if (get_attr(element, "degrees", "yes") == "no") {
        angular_range[0] = angular_range[0] * 180.0 / M_PI;
        angular_range[1] = angular_range[1] * 180.0 / M_PI;
    }

    int N = 100;
    try { N = static_cast<int>(diagram.expr_ctx().eval(get_attr(element, "N", "100")).to_double()); } catch (...) {}

    XmlNode arc_el = diagram.get_scratch().append_child("path");
    auto id_attr = element.attribute("id");
    if (id_attr) diagram.add_id(arc_el, id_attr.value());
    diagram.register_svg_element(element, arc_el);

    auto cmds = make_circle_path(diagram, center, Point2d(radius, radius), angular_range, 0.0, N);

    if (sector) {
        cmds.push_back("L");
        cmds.push_back(pt2str(diagram.transform(center)));
        cmds.push_back("Z");
    }

    std::string d;
    for (const auto& c : cmds) {
        if (!d.empty()) d += " ";
        d += c;
    }
    arc_el.append_attribute("d").set_value(d.c_str());
    add_attr(arc_el, get_2d_attr(element));
    cliptobbox(arc_el, element, diagram);

    // Arrows
    int arrows = 0;
    try { arrows = std::stoi(get_attr(element, "arrows", "0")); }
    catch (...) {
        spdlog::warn("Arrows attribute in <arc> must be an integer");
    }

    std::string forward = "marker-end";
    std::string backward = "marker-start";
    if (get_attr(element, "reverse", "no") == "yes") std::swap(forward, backward);

    std::string aw = get_attr(element, "arrow-width", "");
    std::string aa = get_attr(element, "arrow-angles", "");

    if (arrows > 0) add_arrowhead_to_path(diagram, forward, arc_el, aw, aa);
    if (arrows > 1) add_arrowhead_to_path(diagram, backward, arc_el, aw, aa);

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, arc_el, parent, 2);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, arc_el, parent, 4);
        finish_outline_circle(element, diagram, parent);
    } else {
        parent.append_copy(arc_el);
    }
}

void angle_marker(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_circle(element, diagram, parent);
        return;
    }

    // Styling
    set_attr(element, "stroke", "black");
    if (diagram.output_format() == OutputFormat::Tactile) {
        auto fill_attr = element.attribute("fill");
        if (fill_attr) {
            std::string fv = fill_attr.value();
            std::string lower;
            for (char c : fv) if (c != ' ') lower += std::tolower(c);
            if (lower != "none") fill_attr.set_value("lightgray");
            else fill_attr.set_value("none");
        }
    } else {
        set_attr(element, "fill", "none");
    }
    set_attr(element, "thickness", "2");

    Point2d p_pt, p1_pt, p2_pt;
    auto points_attr = element.attribute("points");
    if (!points_attr) {
        try {
            p_pt = diagram.expr_ctx().eval(element.attribute("p").value()).as_point();
            p1_pt = diagram.expr_ctx().eval(element.attribute("p1").value()).as_point();
            p2_pt = diagram.expr_ctx().eval(element.attribute("p2").value()).as_point();
        } catch (...) {
            spdlog::error("Error in <angle-marker> parsing attributes p, p1, or p2");
            return;
        }
    } else {
        try {
            auto val = diagram.expr_ctx().eval(points_attr.value());
            auto& v = val.as_vector();
            p_pt = Point2d(v[2], v[3]);    // points[1]
            p1_pt = Point2d(v[0], v[1]);   // points[0]
            p2_pt = Point2d(v[4], v[5]);   // points[2]
        } catch (...) {
            spdlog::error("Error in <angle-marker> parsing points={}", points_attr.value());
            return;
        }
    }

    // Check for right angle in user coordinates
    Point2d u_user = normalize(Eigen::VectorXd(p1_pt - p_pt)).head<2>();
    Point2d v_user = normalize(Eigen::VectorXd(p2_pt - p_pt)).head<2>();
    bool right = std::abs(dot(Eigen::VectorXd(u_user), Eigen::VectorXd(v_user))) < 0.001;

    // Convert to SVG coordinates
    Point2d p_svg = diagram.transform(p_pt);
    Point2d p1_svg = diagram.transform(p1_pt);
    Point2d p2_svg = diagram.transform(p2_pt);

    Point2d v1 = normalize(Eigen::VectorXd(p1_svg - p_svg)).head<2>();
    Point2d v2 = normalize(Eigen::VectorXd(p2_svg - p_svg)).head<2>();

    // Cross product z-component for orientation
    int large_arc_flag = (v1[0] * v2[1] - v1[1] * v2[0] > 0) ? 1 : 0;

    double dot_val = v1.dot(v2);
    dot_val = std::clamp(dot_val, -1.0, 1.0);
    double angle_val;
    if (large_arc_flag) {
        angle_val = 2.0 * M_PI - std::acos(dot_val);
    } else {
        angle_val = std::acos(dot_val);
    }

    // Heuristic default radius
    int default_radius_int = static_cast<int>(27.0 / angle_val);
    default_radius_int = std::min(30, default_radius_int);
    default_radius_int = std::max(15, default_radius_int);
    double default_radius = default_radius_int;

    if (diagram.output_format() == OutputFormat::Tactile) {
        default_radius *= 1.5;
    }

    double radius = default_radius;
    try {
        radius = diagram.expr_ctx().eval(get_attr(element, "radius", std::to_string(static_cast<int>(default_radius)))).to_double();
    } catch (...) {}

    double angle2 = std::atan2(v1[1], v1[0]);
    double angle1 = std::atan2(v2[1], v2[0]);

    // Compute label direction
    Point2d sum = v1 + v2;
    Point2d direction;
    if (sum.norm() < 1e-10) {
        // Angle is ~180 degrees
        direction = Point2d(v1[1], -v1[0]);
    } else {
        double sign = (large_arc_flag) ? -1.0 : 1.0;
        direction = sign * normalize(Eigen::VectorXd(sum)).head<2>();
    }

    Point2d label_location = p_svg + direction * radius;
    std::string label_loc_str = pt2str(label_location, ",");
    if (element.attribute("label-location")) {
        element.attribute("label-location").set_value(label_loc_str.c_str());
    } else {
        element.append_attribute("label-location").set_value(label_loc_str.c_str());
    }

    if (!element.attribute("alignment")) {
        std::string align = get_alignment_from_direction(Point2d(direction[0], -direction[1]));
        element.append_attribute("alignment").set_value(align.c_str());
    } else {
        std::string align_str = element.attribute("alignment").value();
        if (align_str == "e") {
            element.attribute("alignment").set_value("east");
        }
    }

    XmlNode arc_el = diagram.get_scratch().append_child("path");
    auto id_attr = element.attribute("id");
    if (id_attr) diagram.add_id(arc_el, id_attr.value());
    diagram.register_svg_element(element, arc_el);

    add_attr(arc_el, get_1d_attr(element));
    cliptobbox(arc_el, element, diagram);

    double thickness = 2.0;
    try { thickness = diagram.expr_ctx().eval(element.attribute("thickness").value()).to_double(); } catch (...) {}

    // Handle arrow on angle marker
    std::string arrow_id;
    if (get_attr(element, "arrow", "no") == "yes") {
        std::string aw = get_attr(element, "arrow-width", "");
        std::string aa = get_attr(element, "arrow-angles", "");
        if (get_attr(element, "reverse", "no") == "yes") {
            arrow_id = add_arrowhead_to_path(diagram, "marker-end", arc_el, aw, aa);
            double arrow_length = get_arrow_length(arrow_id);
            angle2 -= thickness * arrow_length / radius;
        } else {
            arrow_id = add_arrowhead_to_path(diagram, "marker-start", arc_el, aw, aa);
            double arrow_length = get_arrow_length(arrow_id);
            angle1 += thickness * arrow_length / radius;
        }
    }

    // Build the arc path
    std::string d;
    double angle_deg = angle_val * 180.0 / M_PI;
    if (right && angle_deg < 180.0) {
        // Right angle marker
        d = "M " + pt2str(Point2d(p_svg + radius * v1));
        d += " L " + pt2str(Point2d(p_svg + radius * (v1 + v2)));
        d += " L " + pt2str(Point2d(p_svg + radius * v2));
        // No arrow on right angle
        if (element.attribute("arrow")) element.attribute("arrow").set_value("no");
    } else {
        // General arc by sampling
        while (angle2 < angle1) angle2 += 2.0 * M_PI;
        int Narc = 100;
        double dangle = (angle2 - angle1) / Narc;
        double a = angle1;
        Point2d arc_pt = p_svg + radius * Point2d(std::cos(a), std::sin(a));
        std::vector<std::string> arc_cmds;
        arc_cmds.reserve(2 * Narc + 2);
        arc_cmds.push_back("M");
        arc_cmds.push_back(pt2str(arc_pt));
        for (int i = 0; i < Narc; ++i) {
            a += dangle;
            arc_pt = p_svg + radius * Point2d(std::cos(a), std::sin(a));
            arc_cmds.push_back("L " + pt2str(arc_pt));
        }
        d = "";
        for (const auto& cmd : arc_cmds) {
            if (!d.empty()) d += " ";
            d += cmd;
        }
    }
    arc_el.append_attribute("d").set_value(d.c_str());

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, arc_el, parent, 2);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, arc_el, parent, 4);
        finish_outline_circle(element, diagram, parent);
    } else {
        XmlNode original_parent = parent;
        parent = add_label_circle(element, diagram, parent);
        // Copy arc_el from scratch space into the SVG tree
        parent.append_copy(arc_el);

        if (original_parent == parent) return;

        auto eid = element.attribute("id");
        auto pid = parent.attribute("id");
        if (eid && pid && std::string(eid.value()) == std::string(pid.value())) {
            element.remove_attribute("id");
        }
        for (auto child = parent.first_child(); child; child = child.next_sibling()) {
            if (child.attribute("id")) child.remove_attribute("id");
        }
    }
}

static XmlNode add_label_circle(XmlNode element, Diagram& diagram, XmlNode parent) {
    const char* text = element.text().get();
    bool has_text = text != nullptr && std::string(text).find_first_not_of(" \t\n\r") != std::string::npos;

    bool all_comments = true;
    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_comment) {
            all_comments = false;
            break;
        }
    }

    if (has_text || !all_comments) {
        XmlNode group = parent.append_child("g");
        auto id_attr = element.attribute("id");
        if (id_attr) diagram.add_id(group, id_attr.value());

        XmlNode el = group.append_child("label");
        for (auto child = element.first_child(); child; child = child.next_sibling()) {
            el.append_copy(child);
        }

        std::string alignment = get_attr(element, "alignment", "ne");
        el.append_attribute("alignment").set_value(alignment.c_str());

        auto label_loc = element.attribute("label-location");
        if (label_loc) {
            el.append_attribute("p").set_value(label_loc.value());
        }
        el.append_attribute("user-coords").set_value("no");

        auto offset_attr = element.attribute("offset");
        if (offset_attr) {
            el.append_attribute("offset").set_value(offset_attr.value());
        }

        label_element(el, diagram, group, OutlineStatus::None);
        return group;
    }

    return parent;
}

}  // namespace prefigure
