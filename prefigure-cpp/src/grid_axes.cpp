#include "prefigure/grid_axes.hpp"
#include "prefigure/axes.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/line.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <set>
#include <string>
#include <vector>

namespace prefigure {

// ---------------------------------------------------------------------------
// axes_tags
// ---------------------------------------------------------------------------

static const std::set<std::string> s_axes_tags = {"xlabel", "ylabel"};

bool is_axes_tag(const std::string& tag) {
    return s_axes_tags.count(tag) > 0;
}

// ---------------------------------------------------------------------------
// find_gridspacing
// ---------------------------------------------------------------------------

std::array<double, 3> find_gridspacing(
    const std::array<double, 2>& coordinate_range, bool pi_format) {

    double r0 = coordinate_range[0];
    double r1 = coordinate_range[1];
    if (pi_format) {
        r0 /= M_PI;
        r1 /= M_PI;
    }

    double dx = 1.0;
    double distance = std::abs(r1 - r0);
    while (distance > 10.0) {
        distance /= 10.0;
        dx *= 10.0;
    }
    while (distance <= 1.0) {
        distance *= 10.0;
        dx /= 10.0;
    }

    auto& delta = grid_delta_map();
    int key = static_cast<int>(std::round(2.0 * distance));
    auto it = delta.find(key);
    double mult = (it != delta.end()) ? it->second : 0.5;
    dx *= mult;

    double x0, x1;
    if (r1 < r0) {
        dx *= -1.0;
        x0 = dx * std::floor(r0 / dx + 1e-10);
        x1 = dx * std::ceil(r1 / dx - 1e-10);
    } else {
        x0 = dx * std::ceil(r0 / dx - 1e-10);
        x1 = dx * std::floor(r1 / dx + 1e-10);
    }

    if (pi_format) {
        return {x0 * M_PI, dx * M_PI, x1 * M_PI};
    }
    return {x0, dx, x1};
}

// ---------------------------------------------------------------------------
// find_grid_log_positions (grid version with different default spacing)
// ---------------------------------------------------------------------------

std::vector<double> find_grid_log_positions(const std::vector<double>& r) {
    double x0 = std::log10(r.front());
    double x1 = std::log10(r.back());
    double spacing;

    if (r.size() == 3) {
        if (r[1] < 1.0)       spacing = r[1];
        else if (r[1] < 2.0)  spacing = 1.0;
        else if (r[1] < 4.0)  spacing = 2.0;
        else if (r[1] < 7.0)  spacing = 5.0;
        else                   spacing = 10.0;
    } else {
        double width = std::abs(x1 - x0);
        if (width < 1.5)       spacing = 10.0;
        else if (width < 3.0)  spacing = 5.0;
        else if (width < 5.0)  spacing = 2.0;
        else if (width <= 10)  spacing = 1.0;
        else                   spacing = 10.0 / width;
    }

    double ix0 = std::floor(x0);
    double ix1 = std::ceil(x1);
    std::vector<double> positions;

    if (spacing <= 1.0) {
        int gap = static_cast<int>(std::round(1.0 / spacing));
        double x = ix0;
        while (x <= ix1) {
            positions.push_back(std::pow(10.0, x));
            x += gap;
        }
    } else {
        std::vector<int> intermediate;
        if (spacing == 2.0)       intermediate = {1, 5};
        else if (spacing == 5.0)  intermediate = {1, 2, 4, 6, 8};
        else if (spacing == 10.0) intermediate = {1, 2, 3, 4, 5, 6, 7, 8, 9};
        else                      intermediate = {1};

        double x = ix0;
        while (x <= ix1) {
            double base = std::pow(10.0, x);
            for (int c : intermediate) {
                positions.push_back(base * c);
            }
            x += 1.0;
        }
    }
    return positions;
}

// ---------------------------------------------------------------------------
// find_linear_positions
// ---------------------------------------------------------------------------

std::vector<double> find_linear_positions(const std::array<double, 3>& r) {
    int N = static_cast<int>(std::round((r[2] - r[0]) / r[1]));
    std::vector<double> positions;
    positions.reserve(N + 1);
    for (int i = 0; i <= N; ++i) {
        positions.push_back(r[0] + i * (r[2] - r[0]) / N);
    }
    return positions;
}

// ---------------------------------------------------------------------------
// grid_with_basis
// ---------------------------------------------------------------------------

static void finish_outline_grid(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").as_string("black"),
                           element.attribute("thickness").as_string("2"),
                           "none",
                           parent);
}

static void grid_with_basis(XmlNode element, Diagram& diagram, XmlNode parent,
                            const std::string& basis_str, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_grid(element, diagram, parent);
        return;
    }

    Eigen::VectorXd v1, v2;
    try {
        Value val = diagram.expr_ctx().eval(basis_str);
        auto& vec = val.as_vector();
        // Expecting a 4-element vector: v1x, v1y, v2x, v2y
        if (vec.size() == 4) {
            v1 = Eigen::VectorXd(2); v1[0] = vec[0]; v1[1] = vec[1];
            v2 = Eigen::VectorXd(2); v2[0] = vec[2]; v2[1] = vec[3];
        } else {
            spdlog::error("Error in <grid> parsing basis={}", basis_str);
            return;
        }
    } catch (...) {
        spdlog::error("Error in <grid> parsing basis={}", basis_str);
        return;
    }

    if (diagram.output_format() == OutputFormat::Tactile) {
        if (!element.attribute("stroke"))
            element.append_attribute("stroke").set_value("black");
        else
            element.attribute("stroke").set_value("black");
    } else {
        std::string stroke = element.attribute("stroke").as_string("black");
        if (!element.attribute("stroke"))
            element.append_attribute("stroke").set_value(stroke.c_str());
    }
    std::string thick = element.attribute("thickness").as_string("2");
    if (!element.attribute("thickness"))
        element.append_attribute("thickness").set_value(thick.c_str());
    else
        element.attribute("thickness").set_value(thick.c_str());

    std::vector<std::string> cmds;

    // Lines along v2 direction, offset by i*v1
    auto add_lines = [&](const Eigen::VectorXd& offset_vec,
                         const Eigen::VectorXd& dir_vec,
                         int start, int end, int step) {
        for (int i = start; i != end; i += step) {
            Point2d sv(i * offset_vec[0], i * offset_vec[1]);
            Point2d sv_dir(sv[0] + dir_vec[0], sv[1] + dir_vec[1]);
            auto result = infinite_line(sv, sv_dir, diagram);
            if (!result.has_value()) break;
            Point2d p1 = diagram.transform(result->first);
            Point2d p2 = diagram.transform(result->second);
            cmds.push_back("M " + pt2str(p1));
            cmds.push_back("L " + pt2str(p2));
        }
    };

    add_lines(v1, v2, 0, 100, 1);
    add_lines(v1, v2, -1, -100, -1);
    add_lines(v2, v1, 0, 100, 1);
    add_lines(v2, v1, -1, -100, -1);

    auto coords = diagram.get_scratch().append_child("path");
    diagram.add_id(coords, element.attribute("id").as_string(""));
    diagram.register_svg_element(element, coords);

    add_attr(coords, get_1d_attr(element));

    std::string d_str;
    for (const auto& cmd : cmds) {
        if (!d_str.empty()) d_str += " ";
        d_str += cmd;
    }
    coords.append_attribute("d").set_value(d_str.c_str());

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, coords, parent);
        return;
    }

    if (std::string(element.attribute("outline").as_string("no")) == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, coords, parent);
        finish_outline_grid(element, diagram, parent);
    } else {
        parent.append_copy(coords);
    }
}

// ---------------------------------------------------------------------------
// grid
// ---------------------------------------------------------------------------

void grid(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    // Check for basis grid
    std::string basis_str = element.attribute("basis").as_string("");
    if (!basis_str.empty()) {
        grid_with_basis(element, diagram, parent, basis_str, status);
        return;
    }

    std::string thickness = element.attribute("thickness").as_string("1");
    std::string stroke = element.attribute("stroke").as_string("#ccc");
    std::string id = element.attribute("id").as_string("grid");
    id = diagram.prepend_id_prefix(id);

    auto grid_group = parent.append_child("g");
    grid_group.append_attribute("id").set_value(id.c_str());
    grid_group.append_attribute("stroke").set_value(stroke.c_str());
    grid_group.append_attribute("stroke-width").set_value(thickness.c_str());

    diagram.register_svg_element(element, grid_group, false);
    cliptobbox(grid_group, element, diagram);

    BBox bbox = diagram.bbox();
    std::string spacings_str = element.attribute("spacings").as_string("");
    bool h_pi_format = std::string(element.attribute("h-pi-format").as_string("no")) == "yes";
    bool v_pi_format = std::string(element.attribute("v-pi-format").as_string("no")) == "yes";

    std::string coordinates = element.attribute("coordinates").as_string("cartesian");
    auto scales = diagram.get_scales();
    bool hspacings_set = false;

    std::vector<double> x_positions;
    std::vector<double> y_positions;
    std::array<double, 3> rx_arr = {0, 0, 0};

    if (!spacings_str.empty()) {
        try {
            Value val = diagram.expr_ctx().eval(spacings_str);
            auto& vec = val.as_vector();
            // Expecting two sub-ranges packed as a single vector
            // The Python does: rx, ry = un.valid_eval(spacings)
            // This means spacings is a tuple of two tuples
            // We need to handle this - it could be (rx0, rx1, rx2, ry0, ry1, ry2)
            // or two 3-element or 2-element sub-ranges
            if (vec.size() == 6) {
                std::vector<double> rx = {vec[0], vec[1], vec[2]};
                std::vector<double> ry = {vec[3], vec[4], vec[5]};
                if (scales[0] == "log") {
                    x_positions = find_grid_log_positions(rx);
                } else {
                    x_positions = find_linear_positions({rx[0], rx[1], rx[2]});
                }
                if (scales[1] == "log") {
                    y_positions = find_grid_log_positions(ry);
                } else {
                    y_positions = find_linear_positions({ry[0], ry[1], ry[2]});
                }
                rx_arr = {rx[0], rx[1], rx[2]};
                hspacings_set = true;
            } else if (vec.size() == 4) {
                // Two 2-element ranges
                std::vector<double> rx = {vec[0], vec[1]};
                std::vector<double> ry = {vec[2], vec[3]};
                if (scales[0] == "log") {
                    x_positions = find_grid_log_positions(rx);
                } else {
                    auto gs = find_gridspacing({rx[0], rx[1]});
                    x_positions = find_linear_positions(gs);
                }
                if (scales[1] == "log") {
                    y_positions = find_grid_log_positions(ry);
                } else {
                    auto gs = find_gridspacing({ry[0], ry[1]});
                    y_positions = find_linear_positions(gs);
                }
                hspacings_set = true;
            } else {
                spdlog::error("Error in <grid> parsing spacings={}", spacings_str);
                return;
            }
        } catch (...) {
            spdlog::error("Error in <grid> parsing spacings={}", spacings_str);
            return;
        }
    } else {
        // hspacing
        std::string rx_str = element.attribute("hspacing").as_string("");
        if (rx_str.empty()) {
            if (scales[0] == "log") {
                x_positions = find_grid_log_positions({bbox[0], bbox[2]});
            } else {
                auto gs = find_gridspacing({bbox[0], bbox[2]}, h_pi_format);
                rx_arr = gs;
                x_positions = find_linear_positions(gs);
            }
        } else {
            try {
                Value val = diagram.expr_ctx().eval(rx_str);
                auto& vec = val.as_vector();
                std::vector<double> rv(vec.data(), vec.data() + vec.size());
                if (scales[0] == "log") {
                    x_positions = find_grid_log_positions(rv);
                } else {
                    rx_arr = {vec[0], vec[1], vec[2]};
                    x_positions = find_linear_positions(rx_arr);
                }
                hspacings_set = true;
            } catch (...) {
                spdlog::error("Error in <grid> parsing hspacing={}", rx_str);
                return;
            }
        }

        // vspacing / polar angles
        std::array<double, 3> ry_arr = {0, 0, 0};
        if (coordinates == "polar") {
            ry_arr = {0, M_PI / 6.0, 2.0 * M_PI};
        } else {
            std::string ry_str = element.attribute("vspacing").as_string("");
            if (ry_str.empty()) {
                if (scales[1] == "log") {
                    y_positions = find_grid_log_positions({bbox[1], bbox[3]});
                } else {
                    auto gs = find_gridspacing({bbox[1], bbox[3]}, v_pi_format);
                    ry_arr = gs;
                    y_positions = find_linear_positions(gs);
                }
            } else {
                try {
                    Value val = diagram.expr_ctx().eval(ry_str);
                    auto& vec = val.as_vector();
                    std::vector<double> rv(vec.data(), vec.data() + vec.size());
                    if (scales[1] == "log") {
                        y_positions = find_grid_log_positions(rv);
                    } else {
                        ry_arr = {vec[0], vec[1], vec[2]};
                        y_positions = find_linear_positions(ry_arr);
                    }
                } catch (...) {
                    spdlog::error("Error in <grid> parsing vspacing={}",
                                  element.attribute("vspacing").as_string(""));
                    return;
                }
            }
        }

        if (coordinates == "polar") {
            // Set clip path
            std::string clip_id = diagram.get_clippath();
            grid_group.append_attribute("clip-path").set_value(
                std::format("url(#{})", clip_id).c_str());

            BBox pbbox = diagram.bbox();
            std::array<Point2d, 4> endpoints = {
                Point2d(pbbox[0], pbbox[1]),
                Point2d(pbbox[1], pbbox[2]),
                Point2d(pbbox[2], pbbox[3]),
                Point2d(pbbox[3], pbbox[0])
            };
            double R = 0;
            for (auto& ep : endpoints) {
                R = std::max(R, length(Eigen::VectorXd(ep)));
            }
            if (hspacings_set) {
                R = rx_arr[2];
            }

            double r = rx_arr[1];
            if (r <= 0) r = 1; // safety
            int N = 100;
            double dt = 2.0 * M_PI / N;

            while (r <= R) {
                auto circle = grid_group.append_child("path");
                std::string d_str = "M";
                double t = 0;
                Point2d pt = diagram.transform(Point2d(r * std::cos(t), r * std::sin(t)));
                d_str += " " + pt2str(pt);
                for (int i = 0; i < N; ++i) {
                    t += dt;
                    pt = diagram.transform(Point2d(r * std::cos(t), r * std::sin(t)));
                    d_str += " L " + pt2str(pt);
                }
                d_str += " Z";
                circle.append_attribute("d").set_value(d_str.c_str());
                circle.append_attribute("fill").set_value("none");
                r += rx_arr[1];
            }

            // Angular lines
            bool spacing_degrees =
                std::string(element.attribute("spacing-degrees").as_string("no")) == "yes";
            if (spacing_degrees) {
                ry_arr[0] = ry_arr[0] * M_PI / 180.0;
                ry_arr[1] = ry_arr[1] * M_PI / 180.0;
                ry_arr[2] = ry_arr[2] * M_PI / 180.0;
            }

            double t = ry_arr[0];
            while (t <= ry_arr[2]) {
                Point2d direction(std::cos(t), std::sin(t));
                std::vector<double> intersection_times;
                bool vert_close = std::abs(direction[0]) < 1e-10;
                bool horiz_close = std::abs(direction[1]) < 1e-10;

                if (!vert_close) {
                    intersection_times.push_back(pbbox[0] / direction[0]);
                    intersection_times.push_back(pbbox[2] / direction[0]);
                }
                if (!horiz_close) {
                    intersection_times.push_back(pbbox[1] / direction[1]);
                    intersection_times.push_back(pbbox[3] / direction[1]);
                }

                if (!intersection_times.empty()) {
                    double intersection_time = *std::max_element(
                        intersection_times.begin(), intersection_times.end());
                    if (hspacings_set) intersection_time = R;

                    if (intersection_time > 0) {
                        auto line_el = grid_group.append_child("line");
                        Point2d start = diagram.transform(Point2d(0, 0));
                        Point2d end_pt = diagram.transform(
                            Point2d(intersection_time * direction[0],
                                    intersection_time * direction[1]));
                        line_el.append_attribute("x1").set_value(float2str(start[0]).c_str());
                        line_el.append_attribute("y1").set_value(float2str(start[1]).c_str());
                        line_el.append_attribute("x2").set_value(float2str(end_pt[0]).c_str());
                        line_el.append_attribute("y2").set_value(float2str(end_pt[1]).c_str());
                    }
                }
                t += ry_arr[1];
            }
            return;
        }
    }

    // Rectangular grid
    for (double x : x_positions) {
        if (x < bbox[0] || x > bbox[2]) continue;
        XmlNode line_el = mk_line(Point2d(x, bbox[1]), Point2d(x, bbox[3]), diagram);
        line_el.remove_attribute("id");
        grid_group.append_copy(line_el);
    }

    for (double y : y_positions) {
        if (y < bbox[1] || y > bbox[3]) continue;
        XmlNode line_el = mk_line(Point2d(bbox[0], y), Point2d(bbox[2], y), diagram);
        line_el.remove_attribute("id");
        grid_group.append_copy(line_el);
    }
}

// ---------------------------------------------------------------------------
// grid_axes
// ---------------------------------------------------------------------------

void grid_axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    std::string id = element.attribute("id").as_string("grid-axes");
    id = diagram.prepend_id_prefix(id);

    auto group = parent.append_child("g");
    group.append_attribute("id").set_value(id.c_str());
    diagram.register_svg_element(element, group);

    std::string annotation_id = diagram.prepend_id_prefix("grid-axes");
    std::string grid_id = diagram.prepend_id_prefix("grid");
    std::string axes_id = diagram.prepend_id_prefix("axes");

    // Create annotation for the group
    auto scratch = diagram.get_scratch();
    auto group_annotation = scratch.append_child("annotation");
    group_annotation.append_attribute("ref").set_value(annotation_id.c_str());
    group_annotation.append_attribute("text").set_value("The coordinate grid and axes");
    if (std::string(element.attribute("annotate").as_string("yes")) == "yes") {
        diagram.add_default_annotation(group_annotation);
    }

    // Set grid id and render grid
    if (!element.attribute("id"))
        element.append_attribute("id").set_value(grid_id.c_str());
    else
        element.attribute("id").set_value(grid_id.c_str());

    grid(element, diagram, group, status);

    auto grid_annotation = scratch.append_child("annotation");
    grid_annotation.append_attribute("ref").set_value(grid_id.c_str());
    grid_annotation.append_attribute("text").set_value("The coordinate grid");
    group_annotation.append_copy(grid_annotation);

    // Set axes id and render axes
    element.attribute("id").set_value(axes_id.c_str());
    axes(element, diagram, group, status);

    auto axes_annotation = scratch.append_child("annotation");
    axes_annotation.append_attribute("ref").set_value(axes_id.c_str());
    axes_annotation.append_attribute("text").set_value("The coordinate axes");
    group_annotation.append_copy(axes_annotation);
}

}  // namespace prefigure
