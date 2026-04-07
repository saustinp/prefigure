#include "prefigure/point.hpp"
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

// Alignment displacement table (matching Python label.alignment_displacement)
static const std::unordered_map<std::string, std::pair<double, double>> alignment_displacement = {
    {"southeast", {0, 0}},     {"east", {0, 0.5}},       {"northeast", {0, 1}},
    {"north", {-0.5, 1}},     {"northwest", {-1, 1}},   {"west", {-1, 0.5}},
    {"southwest", {-1, 0}},   {"south", {-0.5, 0}},     {"center", {-0.5, 0.5}},
    {"se", {0, 0}},           {"e", {0, 0.5}},          {"ne", {0, 1}},
    {"n", {-0.5, 1}},         {"nw", {-1, 1}},          {"w", {-1, 0.5}},
    {"sw", {-1, 0}},          {"s", {-0.5, 0}},         {"c", {-0.5, 0.5}},
};

// Forward declarations
static void finish_outline_point(XmlNode element, Diagram& diagram, XmlNode parent);
static XmlNode add_label_to_point(XmlNode element, Diagram& diagram, XmlNode parent);

void point(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_point(element, diagram, parent);
        return;
    }

    // Parse point location
    Point2d p;
    try {
        auto val = diagram.expr_ctx().eval(element.attribute("p").value());
        p = val.as_point();

        // Handle polar coordinates
        std::string coords = get_attr(element, "coordinates", "cartesian");
        if (coords == "polar") {
            double radial = p[0];
            double angle = p[1];
            if (get_attr(element, "degrees", "no") == "yes") {
                angle = angle * M_PI / 180.0;
            }
            p = Point2d(radial * std::cos(angle), radial * std::sin(angle));
            // Update the element attribute to cartesian representation
            std::string p_str = std::format("({},{})", p[0], p[1]);
            element.attribute("p").set_value(p_str.c_str());
        }
        p = diagram.transform(p);
    } catch (...) {
        spdlog::error("Error in <point> defining p={}", element.attribute("p").value());
        return;
    }

    // Determine size
    if (diagram.output_format() == OutputFormat::Tactile) {
        auto size_attr = element.attribute("size");
        if (size_attr) {
            double sz = 9.0;
            try {
                sz = diagram.expr_ctx().eval(size_attr.value()).to_double();
            } catch (...) {}
            sz = std::max(sz, 9.0);
            std::string sz_str = std::to_string(static_cast<int>(sz));
            size_attr.set_value(sz_str.c_str());
        } else {
            element.append_attribute("size").set_value("9");
        }
    } else {
        set_attr(element, "size", "4");
    }

    std::string size_str = get_attr(element, "size", "1");
    double size = std::stod(size_str);

    // Determine style
    std::string style = get_attr(element, "style", "circle");

    // Create the shape element in scratch space; it will be copied into
    // the parent tree (or into defs for outline mode) at the end.
    XmlNode scratch = diagram.get_scratch();
    XmlNode shape;
    std::string id_str;
    auto id_attr = element.attribute("id");
    if (id_attr) id_str = id_attr.value();

    if (style == "circle") {
        shape = scratch.append_child("circle");
        diagram.add_id(shape, id_str);
        shape.append_attribute("cx").set_value(float2str(p[0]).c_str());
        shape.append_attribute("cy").set_value(float2str(p[1]).c_str());
        shape.append_attribute("r").set_value(size_str.c_str());
    } else if (style == "box") {
        shape = scratch.append_child("rect");
        diagram.add_id(shape, id_str);
        shape.append_attribute("x").set_value(float2str(p[0] - size).c_str());
        shape.append_attribute("y").set_value(float2str(p[1] - size).c_str());
        shape.append_attribute("width").set_value(float2str(2.0 * size).c_str());
        shape.append_attribute("height").set_value(float2str(2.0 * size).c_str());
    } else if (style == "diamond") {
        shape = scratch.append_child("polygon");
        diagram.add_id(shape, id_str);
        double ds = size * 1.4;
        std::string points;
        points += pt2str(Point2d(p[0], p[1] - ds), ",");
        points += " " + pt2str(Point2d(p[0] + ds, p[1]), ",");
        points += " " + pt2str(Point2d(p[0], p[1] + ds), ",");
        points += " " + pt2str(Point2d(p[0] - ds, p[1]), ",");
        shape.append_attribute("points").set_value(points.c_str());
    } else if (style == "cross") {
        shape = scratch.append_child("path");
        diagram.add_id(shape, id_str);
        double ds = size * 1.4;
        std::string d = "M " + pt2str(Point2d(p[0] - ds, p[1] + ds));
        d += "L " + pt2str(Point2d(p[0] + ds, p[1] - ds));
        d += "M " + pt2str(Point2d(p[0] + ds, p[1] + ds));
        d += "L " + pt2str(Point2d(p[0] - ds, p[1] - ds));
        shape.append_attribute("d").set_value(d.c_str());
    } else if (style == "plus") {
        shape = scratch.append_child("path");
        diagram.add_id(shape, id_str);
        double ds = size * 1.4;
        std::string d = "M " + pt2str(Point2d(p[0] - ds, p[1]));
        d += "L " + pt2str(Point2d(p[0] + ds, p[1]));
        d += "M " + pt2str(Point2d(p[0], p[1] + ds));
        d += "L " + pt2str(Point2d(p[0], p[1] - ds));
        shape.append_attribute("d").set_value(d.c_str());
    } else if (style == "double-circle") {
        shape = scratch.append_child("path");
        diagram.add_id(shape, id_str);
        double r1 = size;
        double indent = std::min(size / 4.0, 9.0);
        double r2 = size - indent;
        if (diagram.output_format() == OutputFormat::Tactile) {
            r2 = size - 9.0;
        }
        std::string r1s = size_str;
        std::string r2s = std::to_string(r2);

        std::string d = "M " + pt2str(Point2d(p[0] - r1, p[1]));
        d += "A " + r1s + " " + r1s + " 0 0 0 " + pt2str(Point2d(p[0] + r1, p[1])) + " ";
        d += "A " + r1s + " " + r1s + " 0 0 0 " + pt2str(Point2d(p[0] - r1, p[1])) + " Z ";
        d += "M " + pt2str(Point2d(p[0] - r2, p[1]));
        d += "A " + r2s + " " + r2s + " 0 0 0 " + pt2str(Point2d(p[0] + r2, p[1])) + " ";
        d += "A " + r2s + " " + r2s + " 0 0 0 " + pt2str(Point2d(p[0] - r2, p[1])) + " Z ";
        shape.append_attribute("d").set_value(d.c_str());
    } else {
        // Default to circle
        shape = scratch.append_child("circle");
        diagram.add_id(shape, id_str);
        shape.append_attribute("cx").set_value(float2str(p[0]).c_str());
        shape.append_attribute("cy").set_value(float2str(p[1]).c_str());
        shape.append_attribute("r").set_value(size_str.c_str());
    }

    // Set styling
    if (diagram.output_format() == OutputFormat::Tactile) {
        std::string fill = get_attr(element, "fill", "");
        if (fill != "none" && fill != "white") {
            set_attr(element, "fill", "lightgray");
        }
        set_attr(element, "stroke", "black");
    } else {
        std::string fill = get_attr(element, "fill", "red");
        if (element.attribute("fill")) {
            element.attribute("fill").set_value(fill.c_str());
        } else {
            element.append_attribute("fill").set_value(fill.c_str());
        }
        std::string stroke = get_attr(element, "stroke", "black");
        if (element.attribute("stroke")) {
            element.attribute("stroke").set_value(stroke.c_str());
        } else {
            element.append_attribute("stroke").set_value(stroke.c_str());
        }
    }
    set_attr(element, "thickness", "2");
    add_attr(shape, get_2d_attr(element));
    cliptobbox(shape, element, diagram);

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, shape, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, shape, parent);
        finish_outline_point(element, diagram, parent);
    } else {
        XmlNode original_parent = parent;
        parent = add_label_to_point(element, diagram, parent);

        // Copy shape from scratch space into the SVG tree
        XmlNode placed = parent.append_copy(shape);

        if (original_parent == parent) {
            diagram.register_svg_element(element, placed);
            return;
        }

        diagram.register_svg_element(element, parent);
        // Remove id from children since it's on the outer <g>
        auto eid = element.attribute("id");
        auto pid = parent.attribute("id");
        if (eid && pid && std::string(eid.value()) == std::string(pid.value())) {
            element.remove_attribute("id");
        }
        for (auto child = parent.first_child(); child; child = child.next_sibling()) {
            if (child.attribute("id")) {
                child.remove_attribute("id");
            }
        }
    }
}

bool inside(const Point2d& p_in, const Point2d& center_in, double size,
            const std::string& style, const CTM& ctm, double buffer) {
    Point2d p = ctm.transform(p_in);
    Point2d center = ctm.transform(center_in);
    Point2d diff = p - center;

    if (style == "circle" || style == "double-circle") {
        return length(Eigen::VectorXd(diff)) < size + buffer;
    }
    if (style == "box" || style == "cross" || style == "plus") {
        double s = size;
        if (style == "cross" || style == "plus") s *= 1.4;
        return std::abs(diff[0]) < s + buffer && std::abs(diff[1]) < s + buffer;
    }
    if (style == "diamond") {
        double s = size * 1.4;
        return std::abs(diff[0] + diff[1]) < s + buffer &&
               std::abs(diff[0] - diff[1]) < s + buffer;
    }
    return false;
}

static void finish_outline_point(XmlNode element, Diagram& diagram, XmlNode parent) {
    XmlNode original_parent = parent;
    parent = add_label_to_point(element, diagram, parent);

    if (original_parent != parent) {
        for (auto child = parent.first_child(); child; child = child.next_sibling()) {
            if (child.attribute("id")) {
                child.remove_attribute("id");
            }
        }
    }

    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

static XmlNode add_label_to_point(XmlNode element, Diagram& diagram, XmlNode parent) {
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

        // Create the label element in scratch space, NOT in the SVG output
        // tree.  In Python this is `el = copy.deepcopy(element); el.tag = 'label'`
        // — a detached element that is only used as a data carrier for the
        // label-placement pipeline.  If we appended it to `group` (which is in
        // the live SVG tree) the source <label> and its <m> children would be
        // serialized verbatim into the output SVG, leaking the source XML.
        XmlNode el = diagram.get_scratch().append_child("label");
        // Copy children
        for (auto child = element.first_child(); child; child = child.next_sibling()) {
            el.append_copy(child);
        }

        // Handle alignment shorthand
        std::string alignment_raw = get_attr(element, "alignment", "");
        if (alignment_raw == "e") alignment_raw = "east";
        std::string alignment = alignment_raw.empty() ? "ne" : alignment_raw;
        el.append_attribute("alignment").set_value(alignment.c_str());

        std::string size_str = get_attr(element, "size", "4");
        double o = std::stod(size_str) + 1.0;

        // Look up displacement
        auto it = alignment_displacement.find(alignment);
        double dx_disp = 0.0, dy_disp = 0.0;
        if (it != alignment_displacement.end()) {
            dx_disp = it->second.first;
            dy_disp = it->second.second;
        }

        double offset_x = 2.0 * o * (dx_disp + 0.5);
        double offset_y = 2.0 * o * (dy_disp - 0.5);

        if (diagram.output_format() == OutputFormat::Tactile) {
            if (offset_x < 0) offset_x -= 6.0;
        } else {
            double cardinal_push = 3.0;
            if (std::abs(offset_x) < 1e-14) {
                if (offset_y > 0) offset_y += cardinal_push;
                if (offset_y < 0) offset_y -= cardinal_push;
            }
            if (std::abs(offset_y) < 1e-14) {
                if (offset_x > 0) offset_x += cardinal_push;
                if (offset_x < 0) offset_x -= cardinal_push;
            }
        }

        // Handle relative offset
        auto rel_offset_attr = element.attribute("offset");
        if (rel_offset_attr) {
            try {
                auto ov = diagram.expr_ctx().eval(rel_offset_attr.value());
                if (ov.is_vector()) {
                    offset_x += ov.as_vector()[0];
                    offset_y += ov.as_vector()[1];
                }
            } catch (...) {}
        }

        el.append_attribute("abs-offset").set_value(
            std::format("({},{})", offset_x, offset_y).c_str());
        el.append_attribute("anchor").set_value(get_attr(element, "p", "(0,0)").c_str());

        label_element(el, diagram, group, OutlineStatus::None);
        return group;
    }

    return parent;
}

}  // namespace prefigure
