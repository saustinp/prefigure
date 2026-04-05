#include "prefigure/arrow.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/repeat.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <string>
#include <unordered_map>

namespace prefigure {

// Global dictionary mapping marker IDs to arrow lengths
static std::unordered_map<std::string, double> arrow_length_dict;

double get_arrow_length(const std::string& key) {
    auto it = arrow_length_dict.find(key);
    if (it != arrow_length_dict.end()) return it->second;
    return 0.0;
}

std::string add_tactile_arrowhead_marker(Diagram& diagram, XmlNode path, bool mid) {
    (void)mid;

    // Get stroke width from the graphical component
    std::string stroke_width_str = get_attr(path, "stroke-width", "1");
    int stroke_width = std::stoi(stroke_width_str);
    std::string id = diagram.prepend_id_prefix("arrow-head-" + stroke_width_str);

    // If we've seen this already, no need to create it again
    if (diagram.has_reusable(id)) return id;

    // Construct the regular (un-outlined) arrow head
    // "angle" is half of the angle at the tip of the head
    double angle_deg = 25.0;
    double A = angle_deg * M_PI / 180.0;
    double t = 1.0;
    double s = 9.0;
    double l = t / std::tan(A) + 0.1;
    double y = s * std::tan(A);

    arrow_length_dict[id] = l;

    // Scale and translate to fit the stroke-width
    CTM ctm;
    ctm.scale(stroke_width, stroke_width);
    ctm.translate(s - l, y);
    Point2d p1 = ctm.transform(Point2d(l, 0));
    Point2d p2 = ctm.transform(Point2d(l - s, y));
    Point2d p3 = ctm.transform(Point2d(l - s, -y));

    std::string d = "M " + pt2str(p1) + " L " + pt2str(p2) + " L " + pt2str(p3) + " Z";

    // Create marker element
    double x2 = l - s;
    double dims_h = 2.0 * y;

    XmlNode defs = diagram.get_defs();
    XmlNode marker = defs.append_child("marker");
    marker.append_attribute("id").set_value(id.c_str());
    marker.append_attribute("markerWidth").set_value(float2str(stroke_width * (l - x2)).c_str());
    marker.append_attribute("markerHeight").set_value(float2str(stroke_width * dims_h).c_str());
    marker.append_attribute("markerUnits").set_value("userSpaceOnUse");
    marker.append_attribute("orient").set_value("auto-start-reverse");
    marker.append_attribute("refX").set_value(float2str(stroke_width * std::abs(x2)).c_str());
    marker.append_attribute("refY").set_value(float2str(stroke_width * dims_h / 2.0).c_str());

    XmlNode marker_path = marker.append_child("path");
    marker_path.append_attribute("d").set_value(d.c_str());
    marker_path.append_attribute("fill").set_value("context-stroke");
    marker_path.append_attribute("stroke").set_value("context-none");

    diagram.add_reusable(marker);

    // Now create the outline marker
    double outline_width = 9.0;

    double push_angle = (M_PI / 2.0) - A;
    Point2d w(outline_width * std::cos(push_angle), outline_width * std::sin(push_angle));
    Point2d q1 = p1 + w;
    Point2d q2 = p2 + w;

    Point2d v(-outline_width, 0.0);
    Point2d q3 = p2 + v;
    Point2d q4 = p3 + v;

    Point2d w_bottom(w[0], -w[1]);
    Point2d q5 = p3 + w_bottom;
    Point2d q6 = p1 + w_bottom;

    // Translate for marker coordinate system
    CTM ctm2;
    ctm2.translate(outline_width, outline_width);
    std::string q1s = pt2str(ctm2.transform(q1));
    std::string q2s = pt2str(ctm2.transform(q2));
    std::string q3s = pt2str(ctm2.transform(q3));
    std::string q4s = pt2str(ctm2.transform(q4));
    std::string q5s = pt2str(ctm2.transform(q5));
    std::string q6s = pt2str(ctm2.transform(q6));

    std::string ow_str = std::to_string(static_cast<int>(outline_width));

    std::string outline_d = "M " + q1s
        + " L " + q2s
        + " A " + ow_str + " " + ow_str + " 0 0 1 " + q3s
        + " L " + q4s
        + " A " + ow_str + " " + ow_str + " 0 0 1 " + q5s
        + " L " + q6s
        + " A " + ow_str + " " + ow_str + " 0 0 1 " + q1s
        + " Z";

    std::string outline_id = id + "-outline";
    XmlNode outline_marker = defs.append_child("marker");
    outline_marker.append_attribute("id").set_value(outline_id.c_str());
    outline_marker.append_attribute("markerWidth").set_value(
        float2str(stroke_width * (l - x2) + 2.0 * outline_width).c_str());
    outline_marker.append_attribute("markerHeight").set_value(
        float2str(stroke_width * dims_h + 2.0 * outline_width).c_str());
    outline_marker.append_attribute("markerUnits").set_value("userSpaceOnUse");
    outline_marker.append_attribute("orient").set_value("auto-start-reverse");
    outline_marker.append_attribute("refX").set_value(
        float2str(std::abs(stroke_width * x2) + outline_width).c_str());
    outline_marker.append_attribute("refY").set_value(
        float2str(stroke_width * dims_h / 2.0 + outline_width).c_str());

    XmlNode outline_path = outline_marker.append_child("path");
    outline_path.append_attribute("d").set_value(outline_d.c_str());
    outline_path.append_attribute("fill").set_value("context-stroke");
    outline_path.append_attribute("stroke").set_value("context-none");

    diagram.add_reusable(outline_marker);
    return id;
}

std::string add_arrowhead_marker(Diagram& diagram, XmlNode path, bool mid,
                                 const std::string& arrow_width_str,
                                 const std::string& arrow_angles_str) {
    double arrow_width = -1.0;  // sentinel: not specified
    if (!arrow_width_str.empty()) {
        try {
            auto val = diagram.expr_ctx().eval(arrow_width_str);
            arrow_width = val.to_double();
        } catch (...) {
            spdlog::error("Error parsing arrow-width={}", arrow_width_str);
            return "";
        }
    }

    double angle_A = 24.0;
    double angle_B = 60.0;
    if (!arrow_angles_str.empty()) {
        try {
            auto val = diagram.expr_ctx().eval(arrow_angles_str);
            if (val.is_vector()) {
                auto& v = val.as_vector();
                angle_A = v[0];
                angle_B = v[1];
            }
        } catch (...) {
            spdlog::error("Error parsing arrow-angles={}", arrow_angles_str);
            return "";
        }
    }

    if (diagram.output_format() == OutputFormat::Tactile) {
        return add_tactile_arrowhead_marker(diagram, path);
    }

    // Get stroke width and color
    std::string stroke_width_str = get_attr(path, "stroke-width", "1");
    int stroke_width = std::stoi(stroke_width_str);
    std::string stroke_color(path.attribute("stroke").value());

    // Build unique ID
    std::string id_data = std::format("_{}_{:.0f}_{:.0f}", arrow_width, angle_A, angle_B);
    std::string id;
    if (!mid) {
        id = diagram.prepend_id_prefix("arrow-head-end-" + stroke_width_str + id_data + "-" + stroke_color);
        if (arrow_width < 0) arrow_width = 4.0;
    } else {
        id = diagram.prepend_id_prefix("arrow-head-mid-" + stroke_width_str + id_data + "-" + stroke_color);
        if (arrow_width < 0) arrow_width = 13.0 / 3.0;
    }

    // Clean the ID for EPUB compatibility
    id = epub_clean(id);

    if (diagram.has_reusable(id)) return id;

    // Construct the pentagon arrow head
    double t = 1.0 / 2.0;
    double s = arrow_width / 2.0;
    double A = angle_A * M_PI / 180.0;
    double B = angle_B * M_PI / 180.0;
    double l = t / std::tan(A) + 0.1;
    double x2 = l - s / std::tan(A);
    double x1 = x2 + (s - t) / std::tan(B);

    arrow_length_dict[id] = l;

    CTM ctm;
    ctm.scale(stroke_width, stroke_width);
    ctm.translate(-x2, s);
    Point2d p1 = ctm.transform(Point2d(l, 0));
    Point2d p2 = ctm.transform(Point2d(x2, s));
    Point2d p3 = ctm.transform(Point2d(x1, t));
    Point2d p4 = ctm.transform(Point2d(x1, -t));
    Point2d p5 = ctm.transform(Point2d(x2, -s));

    std::string d = "M " + pt2str(p1)
        + "L " + pt2str(p2)
        + "L " + pt2str(p3)
        + "L " + pt2str(p4)
        + "L " + pt2str(p5)
        + "Z";

    // Create marker
    XmlNode defs = diagram.get_defs();
    XmlNode marker = defs.append_child("marker");
    marker.append_attribute("id").set_value(id.c_str());
    marker.append_attribute("markerWidth").set_value(float2str(stroke_width * (l - x2)).c_str());
    marker.append_attribute("markerHeight").set_value(float2str(stroke_width * 2.0 * s).c_str());
    marker.append_attribute("markerUnits").set_value("userSpaceOnUse");
    marker.append_attribute("orient").set_value("auto-start-reverse");
    marker.append_attribute("refX").set_value(float2str(stroke_width * std::abs(x2)).c_str());
    marker.append_attribute("refY").set_value(float2str(stroke_width * s).c_str());

    XmlNode marker_path = marker.append_child("path");
    marker_path.append_attribute("d").set_value(d.c_str());
    marker_path.append_attribute("fill").set_value(stroke_color.c_str());
    marker_path.append_attribute("stroke").set_value("none");

    diagram.add_reusable(marker);

    // Create outline marker
    double outline_width = 2.0;
    double push_angle = M_PI / 2.0 - A;
    Point2d w(outline_width * std::cos(push_angle), outline_width * std::sin(push_angle));
    Point2d q1 = p1 + w;
    Point2d q2 = p2 + w;

    Point2d v(-outline_width, 0.0);
    Point2d q3 = p2 + v;
    Point2d q4 = p5 + v;

    Point2d w_bottom(w[0], -w[1]);
    Point2d q5 = p5 + w_bottom;
    Point2d q6 = p1 + w_bottom;

    CTM ctm2;
    ctm2.translate(outline_width, outline_width);
    std::string q1s = pt2str(ctm2.transform(q1));
    std::string q2s = pt2str(ctm2.transform(q2));
    std::string q3s = pt2str(ctm2.transform(q3));
    std::string q4s = pt2str(ctm2.transform(q4));
    std::string q5s = pt2str(ctm2.transform(q5));
    std::string q6s = pt2str(ctm2.transform(q6));

    std::string ow_str = std::to_string(static_cast<int>(outline_width));

    std::string outline_d = "M " + q1s
        + " L " + q2s
        + " A " + ow_str + " " + ow_str + " 0 0 1 " + q3s
        + " L " + q4s
        + " A " + ow_str + " " + ow_str + " 0 0 1 " + q5s
        + " L " + q6s
        + " A " + ow_str + " " + ow_str + " 0 0 1 " + q1s
        + " Z";

    std::string outline_id = id + "-outline";
    XmlNode outline_marker = defs.append_child("marker");
    outline_marker.append_attribute("id").set_value(outline_id.c_str());
    outline_marker.append_attribute("markerWidth").set_value(
        float2str(stroke_width * (l - x2) + 2.0 * outline_width).c_str());
    outline_marker.append_attribute("markerHeight").set_value(
        float2str(stroke_width * 2.0 * s + 2.0 * outline_width).c_str());
    outline_marker.append_attribute("markerUnits").set_value("userSpaceOnUse");
    outline_marker.append_attribute("orient").set_value("auto-start-reverse");
    outline_marker.append_attribute("refX").set_value(
        float2str(std::abs(stroke_width * x2) + outline_width).c_str());
    outline_marker.append_attribute("refY").set_value(
        float2str(stroke_width * s + outline_width).c_str());

    XmlNode outline_path = outline_marker.append_child("path");
    outline_path.append_attribute("d").set_value(outline_d.c_str());
    outline_path.append_attribute("fill").set_value("white");
    outline_path.append_attribute("stroke").set_value("none");

    diagram.add_reusable(outline_marker);
    return id;
}

std::string add_arrowhead_to_path(Diagram& diagram, const std::string& location,
                                  XmlNode path,
                                  const std::string& arrow_width_str,
                                  const std::string& arrow_angles_str) {
    bool mid = (location.size() >= 3 && location.substr(location.size() - 3) == "mid");
    std::string id = add_arrowhead_marker(diagram, path, mid, arrow_width_str, arrow_angles_str);
    if (!id.empty()) {
        std::string url = std::format("url(#{})", id);
        if (path.attribute(location.c_str())) {
            path.attribute(location.c_str()).set_value(url.c_str());
        } else {
            path.append_attribute(location.c_str()).set_value(url.c_str());
        }
    }
    return id;
}

}  // namespace prefigure
