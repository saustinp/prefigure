#include "prefigure/slope_field.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/group.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace prefigure {

// Forward declarations
static void finish_outline_slope(XmlNode element, Diagram& diagram, XmlNode parent);

// Port of Python grid_axes.find_gridspacing
// Returns (x0, dx, x1) where x0 is the first grid line, dx is spacing, x1 is the last
static std::array<double, 3> find_gridspacing(double range_start, double range_end) {
    static const std::unordered_map<int, double> grid_delta = {
        {2, 0.1}, {3, 0.25}, {4, 0.25}, {5, 0.5},
        {6, 0.5}, {7, 0.5}, {8, 0.5}, {9, 0.5}, {10, 0.5},
        {11, 0.5}, {12, 1.0}, {13, 1.0}, {14, 1.0}, {15, 1.0}, {16, 1.0},
        {17, 1.0}, {18, 1.0}, {19, 1.0}, {20, 1.0}
    };

    double dx = 1.0;
    double dist = std::abs(range_end - range_start);
    while (dist > 10.0) {
        dist /= 10.0;
        dx *= 10.0;
    }
    while (dist <= 1.0) {
        dist *= 10.0;
        dx /= 10.0;
    }

    int key = static_cast<int>(std::round(2.0 * dist));
    if (key < 2) key = 2;
    if (key > 20) key = 20;
    auto it = grid_delta.find(key);
    if (it != grid_delta.end()) {
        dx *= it->second;
    }

    double x0, x1;
    if (range_end < range_start) {
        dx *= -1.0;
        x0 = dx * std::floor(range_start / dx + 1e-10);
        x1 = dx * std::ceil(range_end / dx - 1e-10);
    } else {
        x0 = dx * std::ceil(range_start / dx - 1e-10);
        x1 = dx * std::floor(range_end / dx + 1e-10);
    }
    return {x0, dx, x1};
}

void slope_field(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_slope(element, diagram, parent);
        return;
    }

    // Get function
    MathFunction2 f2;
    MathFunction f1;
    bool is_system = get_attr(element, "system", "no") == "yes";

    try {
        auto val = diagram.expr_ctx().eval(element.attribute("function").value());
        if (val.is_function2()) {
            f2 = val.as_function2();
        } else if (val.is_function()) {
            f1 = val.as_function();
        } else {
            spdlog::error("Error retrieving slope-field function={}",
                          element.attribute("function").value());
            return;
        }
    } catch (...) {
        spdlog::error("Error retrieving slope-field function={}",
                      get_attr(element, "function", ""));
        return;
    }

    BBox bbox = diagram.bbox();

    if (!element.attribute("id")) {
        diagram.add_id(element);
    }

    // Turn into a group
    element.set_name("group");
    if (get_attr(element, "outline", "no") == "yes") {
        element.attribute("outline").set_value("always");
    }

    // Create line template
    std::string stroke = "blue";
    if (diagram.output_format() == OutputFormat::Tactile) {
        stroke = "black";
    } else {
        stroke = get_attr(element, "stroke", "blue");
    }
    std::string thickness = get_attr(element, "thickness", "2");
    bool arrows = get_attr(element, "arrows", "no") == "yes";
    std::string arrow_width = get_attr(element, "arrow-width", "");
    std::string arrow_angles = get_attr(element, "arrow-angles", "");

    // Get grid spacings
    std::array<double, 3> rx, ry;
    auto spacings_attr = element.attribute("spacings");
    if (spacings_attr) {
        try {
            auto sv = diagram.expr_ctx().eval(spacings_attr.value());
            auto& v = sv.as_vector();
            // spacings is ((x0,dx,x1), (y0,dy,y1)) or just (dx, dy)
            if (v.size() == 6) {
                rx = {v[0], v[1], v[2]};
                ry = {v[3], v[4], v[5]};
            } else if (v.size() == 2) {
                // Simple spacing: user gives (dx, dy)
                double dx = v[0], dy = v[1];
                rx = {dx * std::ceil(bbox[0] / dx - 1e-10), dx,
                      dx * std::floor(bbox[2] / dx + 1e-10)};
                ry = {dy * std::ceil(bbox[1] / dy - 1e-10), dy,
                      dy * std::floor(bbox[3] / dy + 1e-10)};
            } else {
                spdlog::error("Error parsing slope-field @spacings");
                return;
            }
        } catch (...) {
            spdlog::error("Error parsing slope-field attribute @spacings={}",
                          spacings_attr.value());
            return;
        }
    } else {
        rx = find_gridspacing(bbox[0], bbox[2]);
        ry = find_gridspacing(bbox[1], bbox[3]);
    }

    // Build line elements for each grid point
    double x = rx[0];
    while (x <= rx[2] + 1e-10) {
        double y = ry[0];
        while (y <= ry[2] + 1e-10) {
            double dx_val, dy_val;
            bool add_line = true;

            if (is_system) {
                // f(t, [x,y]) returns [dx, dy]
                try {
                    Eigen::VectorXd state(2);
                    state << x, y;
                    auto change_val = f2(Value(0.0), Value(state));
                    auto change = change_val.as_vector();
                    double change_len = length(change);
                    if (change_len <= 1e-5) {
                        add_line = false;
                    } else {
                        if (std::abs(change[0]) < 1e-8) {
                            dx_val = 0;
                            dy_val = ry[1] / 4.0;
                            if (change[1] < 0) dy_val *= -1;
                        } else {
                            double slope = change[1] / change[0];
                            dx_val = rx[1] / 4.0;
                            dy_val = slope * dx_val;
                            if (std::abs(dy_val) > ry[1] / 4.0) {
                                dy_val = ry[1] / 4.0;
                                dx_val = dy_val / slope;
                            }
                            if (change[0] * dx_val < 0) {
                                dx_val *= -1;
                                dy_val *= -1;
                            }
                        }
                    }
                } catch (...) {
                    add_line = false;
                }
            } else {
                // Scalar slope field: f(x, y) returns slope dy/dx
                double slope;
                bool zero_div = false;
                try {
                    Value result;
                    if (f2) {
                        result = f2(Value(x), Value(y));
                    } else {
                        // Try as single-arg function with vector argument
                        Eigen::VectorXd args(2);
                        args << x, y;
                        result = f1(Value(args));
                    }
                    slope = result.to_double();
                    if (std::isinf(slope) || std::isnan(slope)) {
                        zero_div = true;
                    }
                } catch (...) {
                    zero_div = true;
                    slope = 0;
                }

                if (zero_div) {
                    dx_val = 0;
                    dy_val = ry[1] / 4.0;
                } else {
                    dx_val = rx[1] / 4.0;
                    dy_val = slope * dx_val;
                    if (std::abs(dy_val) > ry[1] / 4.0) {
                        dy_val = ry[1] / 4.0;
                        if (std::abs(slope) > 1e-15) {
                            dx_val = dy_val / slope;
                        }
                    }
                    if (dx_val < 0) {
                        dx_val *= -1;
                        dy_val *= -1;
                    }
                }
            }

            if (add_line) {
                double x0 = x - dx_val;
                double x1_val = x + dx_val;
                double y0 = y - dy_val;
                double y1_val = y + dy_val;

                auto line_el = element.append_child("line");
                line_el.append_attribute("stroke").set_value(stroke.c_str());
                line_el.append_attribute("thickness").set_value(thickness.c_str());
                if (arrows) {
                    line_el.append_attribute("arrows").set_value("1");
                }
                if (!arrow_width.empty()) {
                    line_el.append_attribute("arrow-width").set_value(arrow_width.c_str());
                }
                if (!arrow_angles.empty()) {
                    line_el.append_attribute("arrow-angles").set_value(arrow_angles.c_str());
                }
                line_el.append_attribute("p1").set_value(
                    pt2long_str(Point2d(x0, y0), ",").c_str());
                line_el.append_attribute("p2").set_value(
                    pt2long_str(Point2d(x1_val, y1_val), ",").c_str());
            }

            y += ry[1];
        }
        x += rx[1];
    }

    group(element, diagram, parent, status);
}

void vector_field(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_slope(element, diagram, parent);
        return;
    }

    // Get function
    MathFunction2 f2;
    MathFunction f1;
    bool has_f2 = false;
    try {
        auto val = diagram.expr_ctx().eval(element.attribute("function").value());
        if (val.is_function2()) {
            f2 = val.as_function2();
            has_f2 = true;
        } else if (val.is_function()) {
            f1 = val.as_function();
        } else {
            spdlog::error("Error retrieving vector-field function={}",
                          element.attribute("function").value());
            return;
        }
    } catch (...) {
        spdlog::error("Error retrieving vector-field function={}",
                      get_attr(element, "function", ""));
        return;
    }

    BBox bbox = diagram.bbox();

    if (!element.attribute("id")) {
        diagram.add_id(element);
    }

    element.set_name("group");
    if (get_attr(element, "outline", "no") == "yes") {
        element.attribute("outline").set_value("always");
    }

    std::string stroke = "blue";
    if (diagram.output_format() == OutputFormat::Tactile) {
        stroke = "black";
    } else {
        stroke = get_attr(element, "stroke", "blue");
    }
    std::string thickness = get_attr(element, "thickness", "2");
    std::string arrow_width = get_attr(element, "arrow-width", "");
    std::string arrow_angles = get_attr(element, "arrow-angles", "");

    struct FieldDatum {
        Eigen::VectorXd position;
        Eigen::VectorXd value;
    };
    std::vector<FieldDatum> field_data;
    double scale_factor = 1.0;

    // Curve mode
    auto curve_attr = element.attribute("curve");
    if (curve_attr) {
        MathFunction curve;
        try {
            curve = diagram.expr_ctx().eval(curve_attr.value()).as_function();
        } catch (...) {
            spdlog::error("Error evaluating curve in vector-field");
            return;
        }

        std::array<double, 2> domain;
        try {
            auto dv = diagram.expr_ctx().eval(element.attribute("domain").value());
            auto& v = dv.as_vector();
            domain[0] = v[0]; domain[1] = v[1];
        } catch (...) {
            spdlog::error("A @domain is needed if adding a vector field to a curve");
            return;
        }

        int N_pts;
        try {
            N_pts = static_cast<int>(diagram.expr_ctx().eval(
                element.attribute("N").value()).to_double());
        } catch (...) {
            spdlog::error("A @N is needed if adding a vector field to a curve");
            return;
        }

        // Determine if f is 1-variable or 2-variable
        bool one_variable = true;
        try {
            if (has_f2) {
                one_variable = false;
            } else {
                f1(Value(domain[0]));
            }
        } catch (...) {
            one_variable = false;
        }

        double dt = (domain[1] - domain[0]) / (N_pts - 1);
        double t = domain[0];
        for (int i = 0; i < N_pts; ++i) {
            try {
                auto pos = curve(Value(t)).as_vector();
                Eigen::VectorXd fv;
                if (one_variable) {
                    fv = f1(Value(t)).as_vector();
                } else if (has_f2) {
                    fv = f2(Value(pos[0]), Value(pos[1])).as_vector();
                } else {
                    Eigen::VectorXd args(2);
                    args << pos[0], pos[1];
                    fv = f1(Value(args)).as_vector();
                }
                field_data.push_back({pos, fv});
            } catch (...) {}
            t += dt;
        }

        scale_factor = diagram.expr_ctx().eval(get_attr(element, "scale", "1")).to_double();

    } else {
        // Grid mode
        std::array<double, 3> rx, ry;
        auto spacings_attr = element.attribute("spacings");
        if (spacings_attr) {
            try {
                auto sv = diagram.expr_ctx().eval(spacings_attr.value());
                auto& v = sv.as_vector();
                if (v.size() == 6) {
                    rx = {v[0], v[1], v[2]};
                    ry = {v[3], v[4], v[5]};
                } else if (v.size() == 2) {
                    double dxs = v[0], dys = v[1];
                    rx = {dxs * std::ceil(bbox[0] / dxs - 1e-10), dxs,
                          dxs * std::floor(bbox[2] / dxs + 1e-10)};
                    ry = {dys * std::ceil(bbox[1] / dys - 1e-10), dys,
                          dys * std::floor(bbox[3] / dys + 1e-10)};
                } else {
                    spdlog::error("Error parsing vector-field @spacings");
                    return;
                }
            } catch (...) {
                spdlog::error("Error parsing vector-field attribute @spacings");
                return;
            }
        } else {
            rx = find_gridspacing(bbox[0], bbox[2]);
            ry = find_gridspacing(bbox[1], bbox[3]);
        }

        double exponent = diagram.expr_ctx().eval(get_attr(element, "exponent", "1")).to_double();
        double max_scale = 0.0;

        double x = rx[0];
        while (x <= rx[2] + 1e-10) {
            double y = ry[0];
            while (y <= ry[2] + 1e-10) {
                try {
                    Eigen::VectorXd f_value;
                    if (has_f2) {
                        f_value = f2(Value(x), Value(y)).as_vector();
                    } else {
                        Eigen::VectorXd args(2);
                        args << x, y;
                        f_value = f1(Value(args)).as_vector();
                    }

                    // Check for NaN
                    bool has_nan = false;
                    for (int i = 0; i < f_value.size(); ++i) {
                        if (std::isnan(f_value[i])) { has_nan = true; break; }
                    }
                    if (has_nan) { y += ry[1]; continue; }

                    double norm = length(f_value);
                    if (norm < 1e-10) {
                        f_value = Eigen::VectorXd::Zero(2);
                    } else {
                        f_value = std::pow(norm, exponent) * (1.0 / norm) * f_value;
                    }

                    max_scale = std::max(max_scale,
                        std::max(std::abs(f_value[0] / rx[1]),
                                 std::abs(f_value[1] / ry[1])));

                    Eigen::VectorXd pos(2);
                    pos << x, y;
                    field_data.push_back({pos, f_value});
                } catch (...) {}
                y += ry[1];
            }
            x += rx[1];
        }

        scale_factor = (max_scale > 0) ? std::min(1.0, 0.75 / max_scale) : 1.0;
        if (element.attribute("scale")) {
            scale_factor = diagram.expr_ctx().eval(element.attribute("scale").value()).to_double();
        }
    }

    // Create line elements
    for (const auto& datum : field_data) {
        Eigen::VectorXd v_scaled = scale_factor * datum.value;
        Eigen::VectorXd tail = datum.position;
        Eigen::VectorXd tip = datum.position + v_scaled;

        Point2d p0 = diagram.transform(Point2d(tail[0], tail[1]));
        Point2d p1 = diagram.transform(Point2d(tip[0], tip[1]));
        if (distance(Eigen::VectorXd(p0), Eigen::VectorXd(p1)) < 2.0) continue;

        auto line_el = element.append_child("line");
        line_el.append_attribute("stroke").set_value(stroke.c_str());
        line_el.append_attribute("thickness").set_value(thickness.c_str());
        line_el.append_attribute("arrows").set_value("1");
        if (!arrow_width.empty()) {
            line_el.append_attribute("arrow-width").set_value(arrow_width.c_str());
        }
        if (!arrow_angles.empty()) {
            line_el.append_attribute("arrow-angles").set_value(arrow_angles.c_str());
        }
        line_el.append_attribute("p1").set_value(
            pt2long_str(Point2d(tail[0], tail[1]), ",").c_str());
        line_el.append_attribute("p2").set_value(
            pt2long_str(Point2d(tip[0], tip[1]), ",").c_str());
    }

    group(element, diagram, parent, status);
}

static void finish_outline_slope(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

}  // namespace prefigure
