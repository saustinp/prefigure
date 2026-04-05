#include "prefigure/group.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/utilities.hpp"
#include "prefigure/user_namespace.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>

namespace prefigure {

// Forward declarations for local helpers
static void process_transform(Diagram& diagram, const std::string& transform, XmlNode group, bool tactile);
static void clean_up_transform(Diagram& diagram, XmlNode group, bool tactile);

void group(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus outline_status) {
    auto outline_attr = element.attribute("outline");
    std::string outline = outline_attr ? outline_attr.value() : "";
    bool tactile = (diagram.output_format() == OutputFormat::Tactile);
    auto transform_attr = element.attribute("transform");
    std::string transform = transform_attr ? transform_attr.value() : "";

    diagram.add_id(element);

    std::string format_str = to_string(diagram.output_format());

    if (outline == "always" || outline == format_str) {
        // Two-pass outline rendering
        // First pass: add outlines
        auto group1 = parent.append_child("g");
        diagram.add_id(group1, std::string(element.attribute("id").value()) + "-outline");
        if (!transform.empty()) {
            process_transform(diagram, transform, group1, tactile);
        }
        diagram.parse(element, group1, OutlineStatus::AddOutline);
        if (!transform.empty()) {
            clean_up_transform(diagram, group1, tactile);
        }

        // Second pass: finish outlines (stroke the components)
        auto group2 = parent.append_child("g");
        diagram.add_id(group2, std::string(element.attribute("id").value()));
        diagram.register_svg_element(element, group2);
        if (!transform.empty()) {
            process_transform(diagram, transform, group2, tactile);
        }
        diagram.parse(element, group2, OutlineStatus::FinishOutline);
        if (!transform.empty()) {
            clean_up_transform(diagram, group2, tactile);
        }

        return;
    }

    // Normal (non-outline) group
    auto g = parent.append_child("g");
    diagram.add_id(g, std::string(element.attribute("id").value()));
    diagram.register_svg_element(element, g);

    if (!transform.empty()) {
        process_transform(diagram, transform, g, tactile);
    }
    diagram.parse(element, g, outline_status);
    if (!transform.empty()) {
        clean_up_transform(diagram, g, tactile);
    }
}

static void process_transform(Diagram& diagram, const std::string& transform_raw, XmlNode group, bool tactile) {
    std::string transform = transform_raw;
    // Trim
    auto start = transform.find_first_not_of(" \t\n\r");
    if (start != std::string::npos) {
        auto end = transform.find_last_not_of(" \t\n\r");
        transform = transform.substr(start, end - start + 1);
    }

    if (tactile) {
        diagram.ctm().push();
    }

    auto& ctx = diagram.expr_ctx();

    if (transform.starts_with("translate")) {
        auto index = transform.find('(');
        if (index == std::string::npos) return;
        std::string arg = transform.substr(index);
        Value vec_val = ctx.eval(arg);
        if (!vec_val.is_vector()) return;
        auto vec = vec_val.as_vector();

        if (tactile) {
            diagram.ctm().translate(vec[0], vec[1]);
        } else {
            Point2d p(vec[0], vec[1]);
            Point2d origin(0, 0);
            Point2d diff = diagram.transform(p) - diagram.transform(origin);
            std::string t_string = translatestr(diff[0], diff[1]);
            group.append_attribute("transform").set_value(t_string.c_str());
        }
    }

    if (transform.starts_with("reflect")) {
        auto index = transform.find('(');
        if (index == std::string::npos) return;
        std::string arg = transform.substr(index);
        Value data_val = ctx.eval(arg);
        if (!data_val.is_vector()) return;
        auto data = data_val.as_vector();

        Point2d q1, q2;
        if (data.size() == 4) {
            // Two points: q1 and q2 as (x1,y1,x2,y2) -- actually it's a nested tuple
            // In Python, data is a tuple of two points. In C++, it's a flat vector of 4 elements.
            q1 = Point2d(data[0], data[1]);
            q2 = Point2d(data[2], data[3]);
        } else if (data.size() == 3) {
            // Line Ax + By = C
            double A = data[0], B = data[1], C = data[2];
            if (std::abs(B) < 1e-15) {
                q1 = Point2d(C / A, 0);
                q2 = Point2d(C / A, 1);
            } else {
                q1 = Point2d(0, C / B);
                q2 = Point2d(1, (C - A) / B);
            }
        } else {
            return;
        }

        Point2d p1 = diagram.transform(q1);
        Point2d p2 = diagram.transform(q2);
        Point2d diff = p1 - p2;
        double angle = std::atan2(diff[1], diff[0]) * 180.0 / M_PI;

        if (tactile) {
            diagram.ctm().translate(q1[0], q1[1]);
            diagram.ctm().rotate(-angle);
            diagram.ctm().scale(1, -1);
            diagram.ctm().rotate(angle);
            diagram.ctm().translate(-q1[0], -q1[1]);
        } else {
            std::string t_string = translatestr(p1[0], p1[1]);
            t_string += " " + rotatestr(-angle);
            t_string += " " + scalestr(1, -1);
            t_string += " " + rotatestr(angle);
            t_string += " " + translatestr(-p1[0], -p1[1]);
            group.append_attribute("transform").set_value(t_string.c_str());
        }
    }

    if (transform.starts_with("rotate")) {
        auto index = transform.find('(');
        if (index == std::string::npos) return;
        std::string arg = transform.substr(index);
        Value data_val = ctx.eval(arg);

        double angle_deg;
        Point2d center;
        if (data_val.is_vector() && data_val.as_vector().size() >= 3) {
            // Tuple: (angle, center_point)
            auto& v = data_val.as_vector();
            angle_deg = v[0];
            Point2d center_user(v[1], v[2]);
            center = diagram.transform(center_user);
        } else {
            angle_deg = data_val.to_double();
            center = diagram.transform(Point2d(0, 0));
        }

        if (tactile) {
            Point2d center_user = diagram.inverse_transform(center);
            diagram.ctm().translate(center_user[0], center_user[1]);
            diagram.ctm().rotate(angle_deg);
            diagram.ctm().translate(-center_user[0], -center_user[1]);
        } else {
            std::string t_string = translatestr(center[0], center[1]);
            t_string += " " + rotatestr(angle_deg);
            t_string += " " + translatestr(-center[0], -center[1]);
            group.append_attribute("transform").set_value(t_string.c_str());
        }
    }

    if (transform.starts_with("scale")) {
        auto index = transform.find('(');
        if (index == std::string::npos) return;
        std::string arg = transform.substr(index);
        Value data_val = ctx.eval(arg);

        if (!data_val.is_vector()) return;
        auto data = data_val.as_vector();

        // Last element is center point (2D), preceding elements are scale factors
        // data layout: [sx, (sy,) cx, cy] -- the center is always the last 2D point
        // Actually in Python: data is a list, center = data.pop(-1) which is a 2D point
        // This is tricky -- the Python evaluates a tuple like (sx, sy, center) where center is a point
        // For now, handle the common cases:
        // If 4 elements: sx, sy, cx, cy
        // If 3 elements: s, cx, cy
        double sx, sy;
        Point2d center_point;
        if (data.size() >= 4) {
            sx = data[0];
            sy = data[1];
            center_point = Point2d(data[2], data[3]);
        } else if (data.size() == 3) {
            sx = data[0];
            sy = data[0];
            center_point = Point2d(data[1], data[2]);
        } else {
            return;
        }

        Point2d center = diagram.transform(center_point);

        if (tactile) {
            Point2d center_user = diagram.inverse_transform(center);
            diagram.ctm().translate(center_user[0], center_user[1]);
            diagram.ctm().scale(sx, sy);
            diagram.ctm().translate(-center_user[0], -center_user[1]);
        } else {
            std::string t_string = translatestr(center[0], center[1]);
            t_string += scalestr(sx, sy);
            t_string += translatestr(-center[0], -center[1]);
            group.append_attribute("transform").set_value(t_string.c_str());
        }
    }

    if (transform.starts_with("matrix")) {
        auto index = transform.find('(');
        if (index == std::string::npos) return;
        std::string arg = transform.substr(index);
        Value data_val = ctx.eval(arg);

        if (!data_val.is_vector()) return;
        auto data = data_val.as_vector();

        // Python: data = (matrix, center) where matrix is [[a,b],[c,d]] and center is (x,y)
        // As a flat vector: [m00, m01, m10, m11, cx, cy]
        if (data.size() < 6) return;

        AffineMatrix matrix = {{{data[0], data[1], 0}, {data[2], data[3], 0}}};
        Point2d user_center(data[4], data[5]);
        Point2d center = diagram.transform(user_center);

        if (tactile) {
            diagram.ctm().translate(user_center[0], user_center[1]);
            diagram.ctm().apply_matrix(data[0], data[1], data[2], data[3]);
            diagram.ctm().translate(-user_center[0], -user_center[1]);
        } else {
            std::string t_string = translatestr(center[0], center[1]);
            t_string += matrixstr(matrix);
            t_string += translatestr(-center[0], -center[1]);
            group.append_attribute("transform").set_value(t_string.c_str());
        }
    }
}

static void clean_up_transform(Diagram& diagram, XmlNode group, bool tactile) {
    (void)group;
    if (tactile) {
        diagram.ctm().pop();
    }
}

}  // namespace prefigure
