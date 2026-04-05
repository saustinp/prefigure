#include "prefigure/line.hpp"
#include "prefigure/arrow.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/label.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <vector>

namespace prefigure {

// Forward declarations for internal helpers
static void finish_outline(XmlNode element, Diagram& diagram, XmlNode parent);
static XmlNode add_label_to_line(XmlNode element, Diagram& diagram, XmlNode parent);
static void remove_id(XmlNode el);

void line(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline(element, diagram, parent);
        return;
    }

    Point2d p1, p2;
    auto endpts_attr = element.attribute("endpoints");
    if (!endpts_attr) {
        // Parse p1 and p2 separately
        try {
            auto v1 = diagram.expr_ctx().eval(element.attribute("p1").value());
            p1 = v1.as_point();
        } catch (...) {
            spdlog::error("Error in <line> parsing p1={}", element.attribute("p1").value());
            return;
        }
        try {
            auto v2 = diagram.expr_ctx().eval(element.attribute("p2").value());
            p2 = v2.as_point();
        } catch (...) {
            spdlog::error("Error in <line> parsing p2={}", element.attribute("p2").value());
            return;
        }
    } else {
        try {
            auto val = diagram.expr_ctx().eval(endpts_attr.value());
            // The result should be a vector of length 4 or a pair of 2d points
            auto& v = val.as_vector();
            if (v.size() == 4) {
                p1 = Point2d(v[0], v[1]);
                p2 = Point2d(v[2], v[3]);
            } else {
                spdlog::error("Error in <line> parsing endpoints={}", endpts_attr.value());
                return;
            }
        } catch (...) {
            // Try evaluating as two separate points
            try {
                auto val = diagram.expr_ctx().eval(endpts_attr.value());
                // If it's a nested structure, we need the two sub-vectors
                spdlog::error("Error in <line> parsing endpoints={}", endpts_attr.value());
                return;
            } catch (...) {
                spdlog::error("Error in <line> parsing endpoints={}", endpts_attr.value());
                return;
            }
        }
    }

    Eigen::VectorXd endpoint_offsets;
    std::string infinite_str = get_attr(element, "infinite", "no");
    if (infinite_str == "yes") {
        auto result = infinite_line(p1, p2, diagram);
        if (!result.has_value()) return;
        p1 = result->first;
        p2 = result->second;
    } else {
        auto offset_attr = element.attribute("endpoint-offsets");
        if (offset_attr) {
            try {
                auto val = diagram.expr_ctx().eval(offset_attr.value());
                endpoint_offsets = val.as_vector();
            } catch (...) {
                spdlog::error("Error in <line> parsing endpoint-offsets={}", offset_attr.value());
                return;
            }
        }
    }

    std::string id_str;
    auto id_attr = element.attribute("id");
    if (id_attr) id_str = id_attr.value();

    XmlNode line_el = mk_line(p1, p2, diagram, id_str, endpoint_offsets);
    diagram.register_svg_element(element, line_el);

    // Save endpoints in SVG coordinates for labels
    double x1 = std::stod(line_el.attribute("x1").value());
    double x2 = std::stod(line_el.attribute("x2").value());
    double y1 = std::stod(line_el.attribute("y1").value());
    double y2 = std::stod(line_el.attribute("y2").value());

    Point2d q1(x1, y1);
    Point2d q2(x2, y2);

    // Save data as a vector: [q1x, q1y, q2x, q2y]
    Eigen::VectorXd saved(4);
    saved << q1[0], q1[1], q2[0], q2[1];
    diagram.save_data(element, Value(saved));

    // Set graphical attributes
    set_attr(element, "stroke", "black");
    set_attr(element, "thickness", "2");

    double thickness = 2.0;
    try {
        thickness = diagram.expr_ctx().eval(element.attribute("thickness").value()).to_double();
    } catch (...) {}

    if (diagram.output_format() == OutputFormat::Tactile) {
        if (element.attribute("stroke")) {
            element.attribute("stroke").set_value("black");
        } else {
            element.append_attribute("stroke").set_value("black");
        }
    }
    add_attr(line_el, get_1d_attr(element));

    int arrows = std::stoi(get_attr(element, "arrows", "0"));
    std::string forward = "marker-end";
    std::string backward = "marker-start";
    if (get_attr(element, "reverse", "no") == "yes") {
        std::swap(forward, backward);
    }

    std::string arrow_width_str = get_attr(element, "arrow-width", "");
    std::string arrow_angles_str = get_attr(element, "arrow-angles", "");

    // Adjust endpoints for arrowheads
    if (arrows > 0) {
        std::string arrow_id = add_arrowhead_to_path(
            diagram, forward, line_el, arrow_width_str, arrow_angles_str);

        Point2d lp0(x1, y1);
        Point2d lp1(x2, y2);
        Point2d diff = lp1 - lp0;
        double len = length(diff);
        double ang = std::atan2(diff[1], diff[0]);
        double arrow_len = thickness * get_arrow_length(arrow_id);
        double shortened = len - arrow_len;
        lp1 = lp0 + shortened * Point2d(std::cos(ang), std::sin(ang));
        line_el.attribute("x2").set_value(float2str(lp1[0]).c_str());
        line_el.attribute("y2").set_value(float2str(lp1[1]).c_str());

        if (arrows > 1) {
            add_arrowhead_to_path(diagram, backward, line_el, arrow_width_str, arrow_angles_str);
            lp0 = lp0 + arrow_len * Point2d(std::cos(ang), std::sin(ang));
            line_el.attribute("x1").set_value(float2str(lp0[0]).c_str());
            line_el.attribute("y1").set_value(float2str(lp0[1]).c_str());
        }
    }

    // Handle additional arrows (mid-path arrows)
    auto additional_attr = element.attribute("additional-arrows");
    if (additional_attr) {
        auto additional_val = diagram.expr_ctx().eval(additional_attr.value());
        std::vector<double> additional_list;
        if (additional_val.is_double()) {
            additional_list.push_back(additional_val.as_double());
        } else if (additional_val.is_vector()) {
            auto& v = additional_val.as_vector();
            additional_list.reserve(v.size());
            for (Eigen::Index i = 0; i < v.size(); ++i) {
                additional_list.push_back(v[i]);
            }
        }
        std::sort(additional_list.begin(), additional_list.end());

        // Convert <line> to <path>
        line_el.set_name("path");
        std::string lx1 = line_el.attribute("x1").value();
        std::string ly1 = line_el.attribute("y1").value();
        std::string lx2 = line_el.attribute("x2").value();
        std::string ly2 = line_el.attribute("y2").value();

        Point2d ap1(std::stod(lx1), std::stod(ly1));
        Point2d ap2(std::stod(lx2), std::stod(ly2));

        std::string cmds = "M " + lx1 + " " + ly1;
        for (double t : additional_list) {
            Point2d p = (1.0 - t) * ap1 + t * ap2;
            cmds += " L " + pt2str(p);
        }
        cmds += " L " + lx2 + " " + ly2;
        line_el.append_attribute("d").set_value(cmds.c_str());

        // Remove line-specific attributes
        line_el.remove_attribute("x1");
        line_el.remove_attribute("y1");
        line_el.remove_attribute("x2");
        line_el.remove_attribute("y2");

        add_arrowhead_to_path(diagram, "marker-mid", line_el, arrow_width_str, arrow_angles_str);
    }

    cliptobbox(line_el, element, diagram);

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, line_el, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, line_el, parent);
        finish_outline(element, diagram, parent);
    } else {
        XmlNode original_parent = parent;
        parent = add_label_to_line(element, diagram, parent);
        parent.append_copy(line_el);

        // If no label was added, we're done
        if (original_parent == parent) return;

        // If there is a label, the id is on the outer <g> so remove from children
        remove_id(parent);
    }
}

static void finish_outline(XmlNode element, Diagram& diagram, XmlNode parent) {
    XmlNode original_parent = parent;
    parent = add_label_to_line(element, diagram, parent);

    if (original_parent != parent) {
        remove_id(parent);
    }

    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

static void remove_id(XmlNode el) {
    for (auto child = el.first_child(); child; child = child.next_sibling()) {
        if (child.attribute("id")) {
            child.remove_attribute("id");
        }
        remove_id(child);
    }
}

XmlNode mk_line(const Point2d& p0_in, const Point2d& p1_in, Diagram& diagram,
                const std::string& id, const Eigen::VectorXd& endpoint_offsets,
                bool user_coords) {
    Point2d p0 = p0_in;
    Point2d p1 = p1_in;

    if (user_coords) {
        p0 = diagram.transform(p0);
        p1 = diagram.transform(p1);
    }

    if (endpoint_offsets.size() > 0) {
        if (endpoint_offsets.size() == 2) {
            // 1D offsets along the line direction
            Point2d u = normalize(Eigen::VectorXd(p1 - p0)).head<2>();
            p0 = p0 + endpoint_offsets[0] * u;
            p1 = p1 + endpoint_offsets[1] * u;
        } else if (endpoint_offsets.size() == 4) {
            // 2D absolute offsets: [offset0_x, offset0_y, offset1_x, offset1_y]
            p0[0] += endpoint_offsets[0];
            p0[1] -= endpoint_offsets[1];
            p1[0] += endpoint_offsets[2];
            p1[1] -= endpoint_offsets[3];
        }
    }

    // Create the line element in the diagram's scratch document so it
    // outlives this function.  Callers will append_copy it into the SVG tree.
    XmlNode line_node = diagram.get_scratch().append_child("line");
    diagram.add_id(line_node, id);
    line_node.append_attribute("x1").set_value(float2str(p0[0]).c_str());
    line_node.append_attribute("y1").set_value(float2str(p0[1]).c_str());
    line_node.append_attribute("x2").set_value(float2str(p1[0]).c_str());
    line_node.append_attribute("y2").set_value(float2str(p1[1]).c_str());
    return line_node;
}

std::optional<std::pair<Point2d, Point2d>> infinite_line(
    const Point2d& p0, const Point2d& p1, Diagram& diagram,
    std::optional<double> slope) {

    BBox bbox = diagram.bbox();
    Point2d p = p0;
    Point2d v;
    if (slope.has_value()) {
        v = Point2d(1.0, slope.value());
    } else {
        v = p1 - p0;
    }

    double t_max = std::numeric_limits<double>::infinity();
    double t_min = -std::numeric_limits<double>::infinity();

    if (v[0] != 0.0) {
        double t0 = (bbox[0] - p[0]) / v[0];
        double t1 = (bbox[2] - p[0]) / v[0];
        if (t0 > t1) std::swap(t0, t1);
        t_max = std::min(t1, t_max);
        t_min = std::max(t0, t_min);
    }
    if (v[1] != 0.0) {
        double t0 = (bbox[1] - p[1]) / v[1];
        double t1 = (bbox[3] - p[1]) / v[1];
        if (t0 > t1) std::swap(t0, t1);
        t_max = std::min(t1, t_max);
        t_min = std::max(t0, t_min);
    }

    if (t_min > t_max) return std::nullopt;

    return std::make_pair(p + t_min * v, p + t_max * v);
}

static XmlNode add_label_to_line(XmlNode element, Diagram& diagram, XmlNode parent) {
    // Check for label text
    const char* text = element.text().get();
    bool has_text = text != nullptr && std::string(text).find_first_not_of(" \t\n\r") != std::string::npos;

    // Check if all children are comments
    bool all_comments = true;
    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_comment) {
            all_comments = false;
            break;
        }
    }

    if (has_text || !all_comments) {
        // Bundle label and line in a group
        XmlNode parent_group = parent.append_child("g");
        auto id_attr = element.attribute("id");
        if (id_attr) {
            diagram.add_id(parent_group, id_attr.value());
        }
        diagram.register_svg_element(element, parent_group);

        // Retrieve saved endpoint data
        Value data = diagram.retrieve_data(element);
        Point2d q1, q2;
        if (data.is_vector()) {
            auto& v = data.as_vector();
            q1 = Point2d(v[0], v[1]);
            q2 = Point2d(v[2], v[3]);
        }

        double label_location = 0.5;
        auto loc_attr = element.attribute("label-location");
        if (loc_attr) {
            try {
                label_location = diagram.expr_ctx().eval(loc_attr.value()).to_double();
            } catch (...) {}
        }

        if (label_location < 0) {
            label_location = -label_location;
            std::swap(q1, q2);
        }

        // Create a label element as a copy of the source element
        // In the full implementation, we would deep-copy and set tag to "label"
        // For now, create a new label element and call label::label_element
        XmlNode label_el = parent_group.append_child("label");

        // Copy text and children
        for (auto child = element.first_child(); child; child = child.next_sibling()) {
            label_el.append_copy(child);
        }

        label_el.append_attribute("user-coords").set_value("no");

        Point2d diff = q2 - q1;
        double d = length(Eigen::VectorXd(diff));
        double ang = std::atan2(diff[1], diff[0]) * 180.0 / M_PI;

        if (diagram.output_format() == OutputFormat::Tactile) {
            Point2d anchor = q1 + label_location * diff;
            label_el.append_attribute("anchor").set_value(
                std::format("({},{})", anchor[0], anchor[1]).c_str());
            // Compute alignment from direction
            std::string alignment_val = get_attr(element, "alignment", "north");
            label_el.append_attribute("alignment").set_value(alignment_val.c_str());
            label_element(label_el, diagram, parent_group, OutlineStatus::None);
        } else {
            std::string tform = translatestr(q1[0], q1[1]);
            tform += " " + rotatestr(-ang);
            XmlNode g = parent_group.append_child("g");
            g.append_attribute("transform").set_value(tform.c_str());
            double dist = d * label_location;
            label_el.append_attribute("anchor").set_value(
                std::format("({},0)", dist).c_str());
            std::string alignment_val = get_attr(element, "alignment", "north");
            label_el.append_attribute("alignment").set_value(alignment_val.c_str());
            label_element(label_el, diagram, g, OutlineStatus::None);
        }

        return parent_group;
    }

    return parent;
}

}  // namespace prefigure
