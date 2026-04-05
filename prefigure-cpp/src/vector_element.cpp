#include "prefigure/vector_element.hpp"
#include "prefigure/arrow.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/label.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace prefigure {

// Alignment displacement table (matching Python label.alignment_displacement)
static const std::unordered_map<std::string, std::pair<double, double>> vec_alignment_displacement = {
    {"southeast", {0, 0}},     {"east", {0, 0.5}},       {"northeast", {0, 1}},
    {"north", {-0.5, 1}},     {"northwest", {-1, 1}},   {"west", {-1, 0.5}},
    {"southwest", {-1, 0}},   {"south", {-0.5, 0}},     {"center", {-0.5, 0.5}},
    {"se", {0, 0}},           {"e", {0, 0.5}},          {"ne", {0, 1}},
    {"n", {-0.5, 1}},         {"nw", {-1, 1}},          {"w", {-1, 0.5}},
    {"sw", {-1, 0}},          {"s", {-0.5, 0}},         {"c", {-0.5, 0.5}},
};

// 8-quadrant alignment dictionary: half_quadrant -> (alignment, offset_dir)
static const std::unordered_map<int, std::pair<std::string, int>> alignment_dict = {
    {0, {"se", -1}}, {1, {"nw", 1}}, {2, {"ne", -1}}, {3, {"sw", 1}},
    {4, {"nw", -1}}, {5, {"se", 1}}, {6, {"sw", -1}}, {7, {"ne", 1}}
};

static XmlNode add_label_to_vector(XmlNode element, Diagram& diagram, XmlNode parent);
static void finish_outline_vector(XmlNode element, Diagram& diagram, XmlNode parent);

void vector_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_vector(element, diagram, parent);
        return;
    }

    // Parse @v, @tail, @scale
    Eigen::VectorXd v;
    try {
        v = diagram.expr_ctx().eval(element.attribute("v").value()).as_vector();
    } catch (...) {
        spdlog::error("Error parsing vector attribute @v={}", get_attr(element, "v", ""));
        return;
    }

    Eigen::VectorXd tail;
    try {
        tail = diagram.expr_ctx().eval(get_attr(element, "tail", "[0,0]")).as_vector();
    } catch (...) {
        tail = Eigen::VectorXd::Zero(2);
    }

    double scale_val = diagram.expr_ctx().eval(get_attr(element, "scale", "1")).to_double();
    v = scale_val * v;
    Eigen::VectorXd w = v + tail;

    diagram.register_source_data(element, "v", Value(v));
    diagram.register_source_data(element, "head", Value(w));

    // Head location
    double t_val = -1.0;
    bool has_head_location = false;
    Eigen::VectorXd head_loc;
    auto hl_attr = element.attribute("head-location");
    if (hl_attr) {
        t_val = std::stod(hl_attr.value());
        has_head_location = true;
        head_loc = (1.0 - t_val) * tail + t_val * w;
    }

    // Set default attributes
    if (diagram.output_format() == OutputFormat::Tactile) {
        element.attribute("fill") ?
            element.attribute("fill").set_value("black") :
            element.append_attribute("fill").set_value("black");
        element.attribute("stroke") ?
            element.attribute("stroke").set_value("black") :
            element.append_attribute("stroke").set_value("black");
    } else {
        set_attr(element, "stroke", "black");
        set_attr(element, "fill", "none");
    }
    set_attr(element, "thickness", "3");

    // Create SVG path
    XmlNode vector_path = diagram.get_scratch().append_child("path");
    diagram.add_id(vector_path, get_attr(element, "id", ""));
    diagram.register_svg_element(element, vector_path);
    add_attr(vector_path, get_2d_attr(element));

    // Add arrowhead
    std::string location = has_head_location ? "marker-mid" : "marker-end";
    std::string arrow_id = add_arrowhead_to_path(
        diagram, location, vector_path,
        get_attr(element, "arrow-width", ""),
        get_attr(element, "arrow-angles", ""));

    // Transform endpoints
    Point2d p0 = diagram.transform(Point2d(tail[0], tail[1]));
    Point2d p1 = diagram.transform(Point2d(w[0], w[1]));
    Point2d diff = p1 - p0;
    double len = length(Eigen::VectorXd(diff));
    double angle = std::atan2(diff[1], diff[0]);
    diagram.register_source_data(element, "angle", Value(angle));

    // Pull tip in to accommodate arrowhead
    double arrow_head_length = get_arrow_length(arrow_id);
    double thickness = diagram.expr_ctx().eval(element.attribute("thickness").value()).to_double();
    if (!has_head_location) {
        len -= thickness * arrow_head_length;
        Point2d direction(std::cos(angle), std::sin(angle));
        p1 = p0 + len * direction;
    }

    // Build shaft path
    std::vector<std::string> cmds;
    cmds.push_back("M " + pt2str(p0));
    if (has_head_location) {
        Point2d mid = diagram.transform(Point2d(head_loc[0], head_loc[1]));
        cmds.push_back("L " + pt2str(mid));
    }
    cmds.push_back("L " + pt2str(p1));

    std::string d;
    for (const auto& c : cmds) {
        if (!d.empty()) d += " ";
        d += c;
    }
    vector_path.append_attribute("d").set_value(d.c_str());

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, vector_path, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, vector_path, parent);
        finish_outline_vector(element, diagram, parent);
    } else {
        XmlNode original_parent = parent;
        parent = add_label_to_vector(element, diagram, parent);
        parent.append_copy(vector_path);

        // If no label was added, parent hasn't changed
        if (original_parent == parent) {
            diagram.register_svg_element(element, vector_path);
        } else {
            diagram.register_svg_element(element, parent);
            // Remove duplicate ids from children
            for (auto child = parent.first_child(); child; child = child.next_sibling()) {
                if (child.attribute("id")) {
                    child.remove_attribute("id");
                }
            }
        }
    }
}

static void finish_outline_vector(XmlNode element, Diagram& diagram, XmlNode parent) {
    XmlNode original_parent = parent;
    parent = add_label_to_vector(element, diagram, parent);

    // If label was added, remove duplicate ids
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

static XmlNode add_label_to_vector(XmlNode element, Diagram& diagram, XmlNode parent) {
    // Check if there's label text
    std::string text = element.child_value();
    bool has_text = false;
    {
        std::string trimmed = text;
        auto start = trimmed.find_first_not_of(" \t\n\r");
        if (start != std::string::npos) has_text = true;
    }

    // Check if there are non-comment child elements
    bool all_comments = true;
    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_comment && child.type() != pugi::node_pcdata) {
            all_comments = false;
            break;
        }
    }

    if (!has_text && all_comments) {
        return parent;
    }

    // Create a group to bundle vector + label
    XmlNode grp = parent.append_child("g");
    diagram.add_id(grp, get_attr(element, "id", ""));

    // Build a label element
    pugi::xml_document label_doc;
    auto el = label_doc.append_copy(element);
    el.set_name("label");

    // Determine alignment
    auto alignment_attr = element.attribute("alignment");
    auto user_offset_attr = element.attribute("offset");
    Value angle_val = diagram.get_source_data(element, "angle");
    double angle = angle_val.to_double();

    if (!alignment_attr) {
        double angle_degrees = -angle * 180.0 / M_PI;
        while (angle_degrees < 0) angle_degrees += 360.0;
        int half_quadrant = static_cast<int>(std::floor(angle_degrees / 45.0)) % 8;

        auto it = alignment_dict.find(half_quadrant);
        std::string alignment = it->second.first;
        int offset_dir = it->second.second;

        el.attribute("alignment") ?
            el.attribute("alignment").set_value(alignment.c_str()) :
            el.append_attribute("alignment").set_value(alignment.c_str());

        double normal_x = std::cos(-angle);
        double normal_y = std::sin(-angle);
        double dir_x = static_cast<double>(offset_dir) * (-normal_y);
        double dir_y = static_cast<double>(offset_dir) * (normal_x);
        Point2d offset(4.0 * dir_x, 4.0 * dir_y);

        if (user_offset_attr) {
            auto uoff = diagram.expr_ctx().eval(user_offset_attr.value()).as_point();
            offset += uoff;
        }

        el.attribute("abs-offset") ?
            el.attribute("abs-offset").set_value(np2str(offset).c_str()) :
            el.append_attribute("abs-offset").set_value(np2str(offset).c_str());
    } else {
        std::string alignment = alignment_attr.value();
        auto it = vec_alignment_displacement.find(alignment);
        Point2d displacement(0.0, 0.0);
        if (it != vec_alignment_displacement.end()) {
            displacement = Point2d(it->second.first, it->second.second);
        }
        Point2d def_offset(4.0 * (displacement[0] + 0.5),
                          4.0 * (displacement[1] - 0.5));
        if (user_offset_attr) {
            auto uoff = diagram.expr_ctx().eval(user_offset_attr.value()).as_point();
            def_offset += uoff;
        }
        el.attribute("offset") ?
            el.attribute("offset").set_value(np2str(def_offset).c_str()) :
            el.append_attribute("offset").set_value(np2str(def_offset).c_str());
    }

    // Set anchor to the head of the vector
    Value head_val = diagram.get_source_data(element, "head");
    Point2d head = head_val.as_point();
    std::string anchor_str = pt2long_str(head, ",");

    el.attribute("anchor") ?
        el.attribute("anchor").set_value(anchor_str.c_str()) :
        el.append_attribute("anchor").set_value(anchor_str.c_str());

    // Copy the label element into the group and call label_element
    auto label_node = grp.append_copy(el);
    label_element(label_node, diagram, grp, OutlineStatus::None);

    return grp;
}

}  // namespace prefigure
