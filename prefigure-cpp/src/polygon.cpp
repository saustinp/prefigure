#include "prefigure/polygon.hpp"
#include "prefigure/arrow.hpp"
#include "prefigure/circle.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/group.hpp"
#include "prefigure/label.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/point.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace prefigure {

// Alignment helpers (shared logic)
static const std::vector<std::string> alignment_circle_arr = {
    "east", "northeast", "north", "northwest",
    "west", "southwest", "south", "southeast"
};

static std::string get_alignment_from_direction(const Point2d& direction) {
    double angle = std::atan2(direction[1], direction[0]) * 180.0 / M_PI;
    int align = static_cast<int>(std::round(angle / 45.0)) % 8;
    if (align < 0) align += 8;
    return alignment_circle_arr[align];
}

static void finish_outline_polygon(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

std::optional<std::vector<Point2d>> parse_polygon_points(XmlNode element, Diagram& diagram) {
    auto parameter_attr = element.attribute("parameter");
    auto points_attr_str = get_attr(element, "points", "");

    if (!parameter_attr) {
        // Direct point array
        try {
            auto val = diagram.expr_ctx().eval(points_attr_str);
            if (val.is_vector()) {
                auto& v = val.as_vector();
                std::vector<Point2d> pts;
                pts.reserve(v.size() / 2);
                for (Eigen::Index i = 0; i + 1 < v.size(); i += 2) {
                    pts.emplace_back(v[i], v[i + 1]);
                }
                return pts;
            }
        } catch (...) {
            spdlog::error("Error in <polygon> evaluating points={}", points_attr_str);
            return std::nullopt;
        }
    } else {
        // Parametric generation: parameter="k=0..5", points="(k, k^2)"
        try {
            std::string param_str = parameter_attr.value();
            auto eq_pos = param_str.find('=');
            std::string var = param_str.substr(0, eq_pos);
            std::string range_str = param_str.substr(eq_pos + 1);
            auto dot_pos = range_str.find("..");
            int param_0 = static_cast<int>(diagram.expr_ctx().eval(range_str.substr(0, dot_pos)).to_double());
            int param_1 = static_cast<int>(diagram.expr_ctx().eval(range_str.substr(dot_pos + 2)).to_double());

            std::vector<Point2d> pts;
            for (int k = param_0; k <= param_1; ++k) {
                diagram.expr_ctx().eval(std::to_string(k), var);
                auto pt_val = diagram.expr_ctx().eval(points_attr_str);
                pts.push_back(pt_val.as_point());
            }
            return pts;
        } catch (...) {
            spdlog::error("Error in <polygon> generating points");
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void polygon_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status,
                     const std::vector<Point2d>& points_override,
                     const std::vector<Point2d>& arrow_points) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_polygon(element, diagram, parent);
        return;
    }

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
    }
    set_attr(element, "stroke", "none");
    set_attr(element, "fill", "none");
    set_attr(element, "thickness", "2");

    std::vector<Point2d> points;
    if (!points_override.empty()) {
        points = points_override;
    } else {
        auto parsed = parse_polygon_points(element, diagram);
        if (!parsed.has_value()) return;
        points = std::move(parsed.value());
    }

    // Transform points to SVG coordinates
    std::vector<Point2d> svg_points;
    svg_points.reserve(points.size());
    for (const auto& pt : points) {
        svg_points.push_back(diagram.transform(pt));
    }

    int radius = std::stoi(get_attr(element, "corner-radius", "0"));
    std::string closed = get_attr(element, "closed", "no");

    std::string d;
    if (radius == 0) {
        d = "M " + pt2str(svg_points[0]);
        for (size_t i = 1; i < svg_points.size(); ++i) {
            d += " L " + pt2str(svg_points[i]);
        }
        if (closed == "yes") d += " Z";
    } else {
        // Rounded corners with quadratic Bezier
        auto pts = svg_points;
        if (closed == "yes") pts.push_back(pts[0]);
        int N = static_cast<int>(pts.size()) - 1;

        std::string cmds;
        std::string initial_point_str;
        for (int i = 0; i < N; ++i) {
            Point2d p = pts[i];
            Point2d q = pts[i + 1];
            Point2d u = normalize(Eigen::VectorXd(q - p)).head<2>();
            Point2d p1 = p + static_cast<double>(radius) * u;
            Point2d p2 = q - static_cast<double>(radius) * u;

            if (i == 0) {
                if (closed == "yes") {
                    cmds += "M " + pt2str(p1);
                    initial_point_str = pt2str(p1);
                    cmds += "L " + pt2str(p2);
                } else {
                    cmds += "M " + pt2str(p);
                    cmds += "L " + pt2str(p2);
                }
            }
            if (i == N - 1) {
                cmds += "Q " + pt2str(p) + " " + pt2str(p1);
                if (closed == "yes") {
                    cmds += "L " + pt2str(p2);
                    cmds += "Q " + pt2str(q) + " " + initial_point_str;
                    cmds += "Z";
                } else {
                    cmds += "L" + pt2str(q);
                }
            }
            if (i > 0 && i < N - 1) {
                cmds += "Q" + pt2str(p) + " " + pt2str(p1);
                cmds += "L" + pt2str(p2);
            }
        }
        d = cmds;
    }

    // Handle arrow points (from spline)
    if (!arrow_points.empty()) {
        std::vector<Point2d> ap;
        ap.reserve(arrow_points.size());
        for (const auto& pt : arrow_points) {
            ap.push_back(diagram.transform(pt));
        }
        d += " M " + pt2str(ap[0]);
        for (size_t i = 1; i < ap.size(); ++i) {
            d += " L " + pt2str(ap[i]);
        }
    }

    XmlNode path = diagram.get_scratch().append_child("path");
    auto id_attr = element.attribute("id");
    if (id_attr) diagram.add_id(path, id_attr.value());
    diagram.register_svg_element(element, path);

    path.append_attribute("d").set_value(d.c_str());
    add_attr(path, get_2d_attr(element));

    // Default cliptobbox to yes
    set_attr(element, "cliptobbox", "yes");
    cliptobbox(path, element, diagram);

    // Arrows
    int arrows = std::stoi(get_attr(element, "arrows", "0"));
    std::string forward = "marker-end";
    std::string backward = "marker-start";
    if (get_attr(element, "reverse", "no") == "yes") std::swap(forward, backward);

    std::string aw = get_attr(element, "arrow-width", "");
    std::string aa = get_attr(element, "arrow-angles", "");
    if (arrows > 0) add_arrowhead_to_path(diagram, forward, path, aw, aa);
    if (arrows > 1) add_arrowhead_to_path(diagram, backward, path, aw, aa);

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, path, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, path, parent);
        finish_outline_polygon(element, diagram, parent);
    } else {
        parent.append_copy(path);
    }
}

void spline(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    // TODO: Full cubic spline implementation requires an Eigen tridiagonal solver.
    // For now, implement as a simple polyline through the parsed points.
    auto points_attr = element.attribute("points");
    if (!points_attr) {
        spdlog::error("A spline element needs a @points attribute");
        return;
    }

    auto parsed = parse_polygon_points(element, diagram);
    if (!parsed.has_value()) return;

    auto& points = parsed.value();

    // Parse t-values
    std::vector<double> t_vals;
    auto t_vals_attr = element.attribute("t-values");
    if (!t_vals_attr) {
        t_vals.resize(points.size());
        for (size_t i = 0; i < points.size(); ++i) t_vals[i] = static_cast<double>(i);
    } else {
        auto tv = diagram.expr_ctx().eval(t_vals_attr.value());
        if (tv.is_vector()) {
            auto& v = tv.as_vector();
            t_vals.reserve(v.size());
            for (Eigen::Index i = 0; i < v.size(); ++i) t_vals.push_back(v[i]);
        }
    }

    if (t_vals.size() != points.size()) {
        spdlog::error("The number of t values and points must be the same in a spline");
        return;
    }

    // TODO: Implement CubicSpline using Eigen tridiagonal solver.
    // For now, linearly interpolate between points.
    int N = 100;
    try { N = static_cast<int>(diagram.expr_ctx().eval(get_attr(element, "N", "100")).to_double()); } catch (...) {}

    double t_start = t_vals.front();
    double t_end = t_vals.back();

    auto domain_attr = element.attribute("domain");
    if (domain_attr) {
        auto dv = diagram.expr_ctx().eval(domain_attr.value());
        if (dv.is_vector()) {
            t_start = dv.as_vector()[0];
            t_end = dv.as_vector()[1];
        }
        set_attr(element, "closed", "no");
    }

    // Generate interpolated curve (piecewise linear for now)
    std::vector<Point2d> curve;
    curve.reserve(N);
    double dt = (t_end - t_start) / (N - 1);
    for (int i = 0; i < N; ++i) {
        double t = t_start + i * dt;
        // Find the interval containing t
        size_t idx = 0;
        for (size_t j = 0; j + 1 < t_vals.size(); ++j) {
            if (t >= t_vals[j] && t <= t_vals[j + 1]) {
                idx = j;
                break;
            }
            if (j + 2 == t_vals.size()) idx = j; // clamp to last segment
        }
        double frac = (t_vals[idx + 1] != t_vals[idx])
            ? (t - t_vals[idx]) / (t_vals[idx + 1] - t_vals[idx])
            : 0.0;
        frac = std::clamp(frac, 0.0, 1.0);
        curve.push_back((1.0 - frac) * points[idx] + frac * points[idx + 1]);
    }

    // Convert element tag to polygon for delegation
    element.set_name("polygon");
    polygon_element(element, diagram, parent, status, curve);
}

void triangle(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    // Parse vertices
    Point2d v0, v1, v2;
    try {
        auto val = diagram.expr_ctx().eval(element.attribute("vertices").value());
        auto& v = val.as_vector();
        if (v.size() != 6) {
            spdlog::error("A <triangle> should have exactly 3 vertices");
            return;
        }
        v0 = Point2d(v[0], v[1]);
        v1 = Point2d(v[2], v[3]);
        v2 = Point2d(v[4], v[5]);
    } catch (...) {
        spdlog::error("Error in <triangle> evaluating vertices={}", element.attribute("vertices").value());
        return;
    }

    std::vector<Point2d> vertices = {v0, v1, v2};

    // Turn into a group with polygon + optional angle markers + labels
    // Save a copy for sub-elements
    // In the Python, element is mutated to become a <group> tag
    element.set_name("group");
    if (element.attribute("outline")) {
        element.attribute("outline").set_value("tactile");
    } else {
        element.append_attribute("outline").set_value("tactile");
    }

    // Create polygon child
    XmlNode poly_el = element.append_child("polygon");
    poly_el.append_attribute("closed").set_value("yes");
    poly_el.append_attribute("points").set_value(element.attribute("vertices").value());

    std::string stroke_val = get_attr(element, "stroke", "black");
    poly_el.append_attribute("stroke").set_value(stroke_val.c_str());

    // Copy relevant attributes from parent element
    for (auto attr = element.first_attribute(); attr; attr = attr.next_attribute()) {
        std::string name = attr.name();
        if (name != "vertices" && name != "outline" && name != "stroke" &&
            name != "labels" && name != "show-vertices" && name != "angle-markers" &&
            name != "point-fill" && !poly_el.attribute(name.c_str())) {
            poly_el.append_attribute(name.c_str()).set_value(attr.value());
        }
    }

    // Angle markers
    if (get_attr(element, "angle-markers", "no") == "yes") {
        // Check orientation (cross product)
        Point2d u = v1 - v0;
        Point2d vv = v2 - v1;
        if (u[0] * vv[1] - u[1] * vv[0] > 0) {
            // Reverse vertex order
            std::reverse(vertices.begin(), vertices.end());
        }
        for (int rot = 0; rot < 3; ++rot) {
            XmlNode marker = element.append_child("angle-marker");
            std::string pts_str = "((" + pt2long_str(vertices[0], ",") + "),("
                + pt2long_str(vertices[1], ",") + "),("
                + pt2long_str(vertices[2], ",") + "))";
            marker.append_attribute("points").set_value(pts_str.c_str());
            // Roll vertices
            std::rotate(vertices.begin(), vertices.begin() + 1, vertices.end());
        }
    }

    // Labels
    std::string labels_str = get_attr(element, "labels", "");
    std::vector<std::string> labels;
    if (!labels_str.empty()) {
        // Split by comma
        std::string token;
        for (char c : labels_str) {
            if (c == ',') {
                // Trim
                auto start = token.find_first_not_of(" \t");
                auto end = token.find_last_not_of(" \t");
                if (start != std::string::npos) labels.push_back(token.substr(start, end - start + 1));
                else labels.push_back("");
                token.clear();
            } else {
                token += c;
            }
        }
        auto start = token.find_first_not_of(" \t");
        auto end = token.find_last_not_of(" \t");
        if (start != std::string::npos) labels.push_back(token.substr(start, end - start + 1));
        else labels.push_back("");

        if (labels.size() < 3) {
            spdlog::error("A triangle needs three labels: {}", labels_str);
            return;
        }
    }

    // Compute alignment for labels
    std::unordered_map<int, std::string> alignment_dict;
    if (!labels.empty()) {
        // Build extended vertices array for computing directions
        std::vector<Point2d> ext_verts = {v0, v1, v2, v0, v1};
        for (int i = 1; i <= 3; ++i) {
            Point2d u_dir = ext_verts[i - 1] - ext_verts[i];
            Point2d v_dir = ext_verts[i + 1] - ext_verts[i];
            Point2d dir = -(u_dir + v_dir);
            alignment_dict[i % 3] = get_alignment_from_direction(dir);
        }
    }

    // Show vertices or just labels
    if (get_attr(element, "show-vertices", "no") == "yes") {
        std::vector<Point2d> orig_verts = {v0, v1, v2};
        for (int i = 0; i < 3; ++i) {
            XmlNode point_el = element.append_child("point");
            point_el.append_attribute("p").set_value(pt2long_str(orig_verts[i], ",").c_str());
            std::string fill = get_attr(element, "point-fill", "");
            if (!fill.empty()) point_el.append_attribute("fill").set_value(fill.c_str());
            if (alignment_dict.count(i) && !labels.empty()) {
                XmlNode m_tag = point_el.append_child("m");
                m_tag.append_child(pugi::node_pcdata).set_value(labels[i].c_str());
                point_el.append_attribute("alignment").set_value(alignment_dict[i].c_str());
            }
        }
    } else if (!labels.empty()) {
        std::vector<Point2d> orig_verts = {v0, v1, v2};
        for (int i = 0; i < 3; ++i) {
            XmlNode label_el = element.append_child("label");
            label_el.append_attribute("anchor").set_value(pt2long_str(orig_verts[i], ",").c_str());
            if (alignment_dict.count(i)) {
                label_el.append_attribute("alignment").set_value(alignment_dict[i].c_str());
            }
            XmlNode m_tag = label_el.append_child("m");
            m_tag.append_child(pugi::node_pcdata).set_value(labels[i].c_str());
        }
    }

    // Delegate to group handler
    group(element, diagram, parent, status);
}

}  // namespace prefigure
