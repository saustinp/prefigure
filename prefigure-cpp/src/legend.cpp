#include "prefigure/legend.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/label.hpp"
#include "prefigure/utilities.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/user_namespace.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>

namespace prefigure {

// ---------------------------------------------------------------------------
// Legend construction
// ---------------------------------------------------------------------------

Legend::Legend(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus outline_status)
    : element_(element)
    , diagram_(&diagram)
    , parent_(parent)
    , outline_(outline_status == OutlineStatus::AddOutline)
    , tactile_(diagram.output_format() == OutputFormat::Tactile)
{
    // Create group
    group_ = parent.append_child("g");
    diagram.add_id(group_, element.attribute("id").as_string());
    diagram.register_svg_element(element, group_);

    // Register this legend for later placement
    diagram.add_legend(this);

    // Parse anchor
    std::string anchor_str = get_attr(element, "anchor", "(bbox[2],bbox[3])");
    try {
        Value user_anchor = diagram.expr_ctx().eval(anchor_str);
        def_anchor_ = diagram.transform(user_anchor.as_point());
    } catch (const std::exception& e) {
        spdlog::error("Error in <legend> evaluating anchor={}", anchor_str);
        return;
    }

    // Parse alignment
    std::string alignment_str = get_attr(element, "alignment", "c");
    if (alignment_str == "e") {
        element.attribute("alignment").set_value("east");
        if (!element.attribute("alignment")) {
            element.append_attribute("alignment").set_value("east");
        }
        alignment_str = "east";
    }
    alignment_str = get_attr(element, "alignment", "c");

    auto& disp_map = alignment_displacement_map();
    auto it = disp_map.find(alignment_str);
    if (it != disp_map.end()) {
        displacement_ = it->second;
    } else {
        displacement_ = {-0.5, 0.5};  // center default
    }

    // Line key width
    line_width_ = tactile_ ? 72.0 : 24.0;
    double point_width = 10.0;

    // Process child <item> elements
    for (auto li = element.first_child(); li; li = li.next_sibling()) {
        if (std::string(li.name()) != "item") {
            spdlog::warn("{} is not allowed inside a <legend>", li.name());
            continue;
        }

        // Create the label element (a copy with tag changed to "label")
        // In C++ with pugixml we can't easily rename tags, so we create a new element
        auto label_id_stub = diagram.prepend_id_prefix("legend-label");
        int num = static_cast<int>(li_items_.size());

        // Create label element in scratch
        auto scratch = diagram.get_scratch();
        auto label_el = scratch.append_child("label");
        // Copy attributes from li
        for (auto attr = li.first_attribute(); attr; attr = attr.next_attribute()) {
            label_el.append_attribute(attr.name()).set_value(attr.value());
        }
        // Copy children
        for (auto child = li.first_child(); child; child = child.next_sibling()) {
            label_el.append_copy(child);
        }

        std::string label_id = label_id_stub + "-" + std::to_string(num);
        if (label_el.attribute("id")) {
            label_el.attribute("id").set_value(label_id.c_str());
        } else {
            label_el.append_attribute("id").set_value(label_id.c_str());
        }
        label_el.append_attribute("alignment").set_value("se");
        label_el.append_attribute("anchor").set_value(anchor_str.c_str());
        label_el.append_attribute("abs-offset").set_value("(0,0)");
        label_el.append_attribute("justify").set_value("left");

        // Process the label (registers with MathJax etc.)
        auto dummy_group = scratch.append_child("g");
        label_element(label_el, diagram, dummy_group, OutlineStatus::None);
        scratch.remove_child(dummy_group);

        // Determine the key element by looking up the ref'd element
        std::string ref = label_el.attribute("ref").as_string();
        XmlNode key_el;

        // Look up the referenced element in the diagram source by id or at
        XmlNode ref_element;
        if (!ref.empty()) {
            // Search by id attribute
            ref_element = diagram.get_diagram_element().find_node(
                [&ref](pugi::xml_node n) {
                    return std::string(n.attribute("id").as_string()) == ref;
                });
            if (!ref_element) {
                // Search by at attribute
                ref_element = diagram.get_diagram_element().find_node(
                    [&ref](pugi::xml_node n) {
                        return std::string(n.attribute("at").as_string()) == ref;
                    });
            }
            if (!ref_element) {
                spdlog::warn("{} should refer to an element", ref);
            }
        }

        if (ref_element && std::string(ref_element.name()) == "point") {
            // Point key: small filled circle
            key_el = scratch.append_child("point");
            key_el.append_attribute("p").set_value(anchor_str.c_str());
            key_el.append_attribute("size").set_value("4");
            std::string point_id_stub = diagram.prepend_id_prefix("legend-point");
            key_el.append_attribute("id").set_value(
                (point_id_stub + "-" + std::to_string(num)).c_str());
            // Copy visual attributes from referenced point
            for (auto attr : {"fill", "stroke", "style"}) {
                auto a = ref_element.attribute(attr);
                if (a) key_el.append_attribute(attr).set_value(a.value());
            }
            key_width_ = std::max(key_width_, point_width);
        } else if (ref_element) {
            std::string ref_fill = get_attr(ref_element, "fill", "none");
            if (ref_fill != "none") {
                // Filled shape key: filled box
                key_el = scratch.append_child("point");
                key_el.append_attribute("stroke").set_value(
                    get_attr(ref_element, "stroke", "none").c_str());
                key_el.append_attribute("fill").set_value(ref_fill.c_str());
                key_el.append_attribute("style").set_value(
                    get_attr(ref_element, "style", "box").c_str());
                key_el.append_attribute("size").set_value("5");
                key_width_ = std::max(key_width_, point_width);
            } else {
                // Line key
                key_el = scratch.append_child("line");
                key_el.append_attribute("stroke").set_value(
                    get_attr(ref_element, "stroke", "none").c_str());
                std::string dash = get_attr(ref_element, "dash", "");
                if (!dash.empty()) {
                    key_el.append_attribute("stroke-dasharray").set_value(dash.c_str());
                }
                key_width_ = std::max(key_width_, line_width_);
            }
        } else {
            // Fallback: simple line key
            key_el = scratch.append_child("line");
            key_el.append_attribute("stroke").set_value("black");
            key_width_ = std::max(key_width_, line_width_);
        }

        ItemEntry entry;
        entry.key = key_el;
        entry.label = label_el;
        li_items_.push_back({li, entry});
    }
}

// ---------------------------------------------------------------------------
// place_legend (SVG)
// ---------------------------------------------------------------------------

void Legend::place_legend(Diagram& diagram) {
    if (tactile_) {
        place_tactile_legend(diagram);
        return;
    }

    double outer_padding = 5.0;
    double center_padding = 10.0;
    double interline = 7.0;
    try {
        std::string vs = get_attr(element_, "vertical-skip", "7");
        interline = diagram.expr_ctx().eval(vs).to_double();
    } catch (...) {
        interline = 7.0;
    }

    double height = outer_padding;
    double label_width = 0;

    for (auto& [li, entry] : li_items_) {
        auto dims = diagram.get_label_dims(entry.label);
        if (dims.first == 0 && dims.second == 0) {
            spdlog::warn("There is a missing label in a <legend>");
            continue;
        }
        height += dims.second + interline;
        label_width = std::max(label_width, dims.first);
    }
    height += outer_padding - interline;

    double width = label_width + 2 * outer_padding + key_width_ + center_padding;

    // Offset from anchor
    std::array<double, 2> offset = {
        8.0 * (displacement_[0] + 0.5),
        8.0 * (displacement_[1] - 0.5)
    };

    // Build transform
    auto& p = def_anchor_;
    std::string tform = translatestr(p[0] + offset[0], p[1] - offset[1]);

    double scale = 1.0;
    try {
        scale = std::stod(get_attr(element_, "scale", "1"));
    } catch (...) {}

    double dx = scale * width * displacement_[0];
    double dy = scale * height * displacement_[1];
    tform += " " + translatestr(dx, -dy);
    tform += " " + scalestr(scale, scale);

    group_.append_attribute("transform").set_value(tform.c_str());

    // Bounding box rectangle
    auto rect = group_.append_child("rect");
    rect.append_attribute("x").set_value("0");
    rect.append_attribute("y").set_value("0");
    rect.append_attribute("width").set_value(std::to_string(width).c_str());
    rect.append_attribute("height").set_value(std::to_string(height).c_str());
    rect.append_attribute("stroke").set_value(
        get_attr(element_, "stroke", "black").c_str());
    rect.append_attribute("fill").set_value("white");
    rect.append_attribute("fill-opacity").set_value(
        get_attr(element_, "opacity", "1").c_str());

    // Place labels and keys
    double label_x = outer_padding + key_width_ + center_padding;
    double y = outer_padding;

    for (auto& [li, entry] : li_items_) {
        auto dims = diagram.get_label_dims(entry.label);
        if (dims.first == 0 && dims.second == 0) continue;

        // Retrieve the actual label group from diagram's label_group_dict
        auto [label_group, label_el_node, label_ctm] = diagram.get_label_group(entry.label);
        if (label_group) {
            std::string label_tform = translatestr(label_x, y);
            label_group.append_attribute("transform").set_value(label_tform.c_str());
            group_.append_copy(label_group);
        } else {
            // Fallback: create empty placeholder
            auto label_g = group_.append_child("g");
            std::string label_tform = translatestr(label_x, y);
            label_g.append_attribute("transform").set_value(label_tform.c_str());
        }

        // Position key
        double key_y = y + dims.second / 2.0;
        std::string key_tag = entry.key.name();

        if (key_tag == "point") {
            double key_x = outer_padding + key_width_ / 2.0;
            Point2d user_point = diagram.inverse_transform(Point2d(key_x, key_y));
            entry.key.attribute("p") ?
                entry.key.attribute("p").set_value(pt2str(user_point, ",").c_str()) :
                entry.key.append_attribute("p").set_value(pt2str(user_point, ",").c_str());
            // Process the point element into the legend group
            // For now, append a circle representation
            auto key_copy = group_.append_child("circle");
            key_copy.append_attribute("cx").set_value(std::to_string(key_x).c_str());
            key_copy.append_attribute("cy").set_value(std::to_string(key_y).c_str());
            std::string size = entry.key.attribute("size").as_string("4");
            key_copy.append_attribute("r").set_value(size.c_str());
            auto fill_a = entry.key.attribute("fill");
            if (fill_a) key_copy.append_attribute("fill").set_value(fill_a.value());
            else key_copy.append_attribute("fill").set_value("black");
            auto stroke_a = entry.key.attribute("stroke");
            if (stroke_a) key_copy.append_attribute("stroke").set_value(stroke_a.value());

            std::string style = entry.key.attribute("style").as_string("");
            if (style == "box") {
                // Replace circle with rect for box style
                group_.remove_child(key_copy);
                auto rect = group_.append_child("rect");
                double sz = std::stod(size);
                rect.append_attribute("x").set_value(std::to_string(key_x - sz).c_str());
                rect.append_attribute("y").set_value(std::to_string(key_y - sz).c_str());
                rect.append_attribute("width").set_value(std::to_string(2 * sz).c_str());
                rect.append_attribute("height").set_value(std::to_string(2 * sz).c_str());
                if (fill_a) rect.append_attribute("fill").set_value(fill_a.value());
                else rect.append_attribute("fill").set_value("black");
                if (stroke_a) rect.append_attribute("stroke").set_value(stroke_a.value());
            }
        } else if (key_tag == "line") {
            double key_x0 = outer_padding;
            double key_x1 = outer_padding + line_width_;
            auto key_copy = group_.append_copy(entry.key);
            key_copy.append_attribute("x1").set_value(std::to_string(key_x1).c_str());
            key_copy.append_attribute("y1").set_value(std::to_string(key_y).c_str());
            key_copy.append_attribute("x2").set_value(std::to_string(key_x0).c_str());
            key_copy.append_attribute("y2").set_value(std::to_string(key_y).c_str());
            key_copy.append_attribute("stroke-width").set_value("2");
        }

        y += dims.second + interline;
    }
}

// ---------------------------------------------------------------------------
// place_tactile_legend
// ---------------------------------------------------------------------------

void Legend::place_tactile_legend(Diagram& diagram) {
    double gap = 3.6;
    double outer_padding = 3.0 * gap;
    double center_padding = 6.0 * gap;
    double interline = 4.0 * gap;

    double height = outer_padding;
    double label_width = 0;

    for (auto& [li, entry] : li_items_) {
        auto dims = diagram.get_label_dims(entry.label);
        height += dims.second + interline;
        label_width = std::max(label_width, dims.first);
    }
    height += outer_padding - interline;

    double width = label_width + 2 * outer_padding + key_width_ + center_padding;

    // Offset
    std::array<double, 2> offset = {
        8.0 * (displacement_[0] + 0.5),
        8.0 * (displacement_[1] - 0.5)
    };

    auto sign_fn = [](double x) -> double {
        return (x > 0) ? 1.0 : (x < 0) ? -1.0 : 0.0;
    };
    offset[0] += 6.0 * sign_fn(offset[0]);
    offset[1] += 6.0 * sign_fn(offset[1]);
    if (displacement_[0] == 0) offset[0] += 6;
    if (displacement_[1] == -1) offset[1] -= 6;

    auto& p = def_anchor_;
    double dx = width * displacement_[0];
    double dy = height * displacement_[1];

    double tx = p[0] + offset[0] + dx;
    double ty = p[1] - offset[1] - dy;
    tx = gap * std::round(tx / gap);
    ty = gap * std::round(ty / gap);

    std::string tform = translatestr(tx, ty);
    group_.append_attribute("transform").set_value(tform.c_str());

    // Bounding rectangle
    auto rect = group_.append_child("rect");
    rect.append_attribute("x").set_value("0");
    rect.append_attribute("y").set_value("0");
    rect.append_attribute("width").set_value(std::to_string(width).c_str());
    rect.append_attribute("height").set_value(std::to_string(height).c_str());
    rect.append_attribute("stroke").set_value(
        get_attr(element_, "stroke", "black").c_str());
    rect.append_attribute("fill").set_value("white");

    // Place labels and keys
    double label_x = outer_padding + key_width_ + center_padding;
    double y = outer_padding;

    for (auto& [li, entry] : li_items_) {
        auto dims = diagram.get_label_dims(entry.label);
        if (dims.first == 0 && dims.second == 0) continue;

        double lx = gap * std::round(label_x / gap);
        double ly = gap * std::round(y / gap);

        // Retrieve the actual label group from diagram's label_group_dict
        auto [label_group, label_el_node, label_ctm] = diagram.get_label_group(entry.label);
        if (label_group) {
            std::string label_tform = translatestr(lx, ly);
            label_group.append_attribute("transform").set_value(label_tform.c_str());
            group_.append_copy(label_group);
        } else {
            std::string label_tform = translatestr(lx, ly);
            auto label_g = group_.append_child("g");
            label_g.append_attribute("transform").set_value(label_tform.c_str());
        }

        double key_y = y + dims.second / 2.0;
        std::string key_tag = entry.key.name();

        if (key_tag == "line") {
            double key_x0 = outer_padding;
            double key_x1 = outer_padding + line_width_;
            auto key_copy = group_.append_copy(entry.key);
            key_copy.append_attribute("x1").set_value(std::to_string(key_x1).c_str());
            key_copy.append_attribute("y1").set_value(std::to_string(key_y).c_str());
            key_copy.append_attribute("x2").set_value(std::to_string(key_x0).c_str());
            key_copy.append_attribute("y2").set_value(std::to_string(key_y).c_str());
            key_copy.append_attribute("stroke-width").set_value("2");
        }

        y += dims.second + interline;
    }
}

// ---------------------------------------------------------------------------
// legend_element (entry point)
// ---------------------------------------------------------------------------

void legend_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        return;
    }

    // The Legend constructor registers itself with the diagram
    // It will be placed later via place_legend()
    // We allocate on the heap; diagram takes ownership via add_legend
    new Legend(element, diagram, parent, status);
}

}  // namespace prefigure
