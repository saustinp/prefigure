#include "prefigure/graph.hpp"
#include "prefigure/arrow.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <limits>
#include <string>
#include <vector>

namespace prefigure {

// Forward declarations
static void finish_outline_graph(XmlNode element, Diagram& diagram, XmlNode parent);
static std::vector<std::string> cartesian_path(XmlNode element, Diagram& diagram,
                                                const MathFunction& f,
                                                const std::array<double, 2>& domain, int N);
static std::vector<std::string> polar_path(XmlNode element, Diagram& diagram,
                                            const MathFunction& f,
                                            const std::array<double, 2>& domain, int N);

void graph(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_graph(element, diagram, parent);
        return;
    }

    bool polar = get_attr(element, "coordinates", "cartesian") == "polar";
    BBox bbox = diagram.bbox();

    std::array<double, 2> domain;
    auto domain_attr = element.attribute("domain");
    if (!domain_attr) {
        if (polar) {
            domain = {0.0, 2.0 * M_PI};
        } else {
            domain = {bbox[0], bbox[2]};
        }
    } else {
        auto dv = diagram.expr_ctx().eval(domain_attr.value());
        if (dv.is_vector()) {
            auto& v = dv.as_vector();
            domain[0] = v[0];
            domain[1] = v[1];
            if (std::isinf(domain[0]) && domain[0] < 0) domain[0] = bbox[0];
            if (std::isinf(domain[1]) && domain[1] > 0) domain[1] = bbox[2];
        }
    }

    // Adjust domain for arrows
    int arrows = std::stoi(get_attr(element, "arrows", "0"));
    if (arrows > 0 && !polar) {
        Point2d end_pt = diagram.transform(Point2d(domain[1], 0.0));
        end_pt[0] -= 2.0;
        Point2d new_domain = diagram.inverse_transform(end_pt);
        domain[1] = new_domain[0];
    }
    if (arrows == 2 && !polar) {
        Point2d begin_pt = diagram.transform(Point2d(domain[0], 0.0));
        begin_pt[0] += 2.0;
        Point2d new_domain = diagram.inverse_transform(begin_pt);
        domain[0] = new_domain[0];
    }

    // Get function
    MathFunction f;
    try {
        auto val = diagram.expr_ctx().eval(element.attribute("function").value());
        if (val.is_function()) {
            f = val.as_function();
        } else {
            spdlog::error("Error retrieving function in graph");
            return;
        }
    } catch (std::exception& e) {
        spdlog::error("Error retrieving function in graph: {}", e.what());
        return;
    }

    int N = std::stoi(get_attr(element, "N", "100"));

    std::vector<std::string> cmds;
    if (polar) {
        cmds = polar_path(element, diagram, f, domain, N);
    } else {
        cmds = cartesian_path(element, diagram, f, domain, N);
    }

    // Set attributes
    set_attr(element, "thickness", "2");
    set_attr(element, "stroke", "blue");
    if (diagram.output_format() == OutputFormat::Tactile) {
        element.attribute("stroke").set_value("black");
    }

    std::string id_str = diagram.find_id(element, get_attr(element, "id", ""));
    auto attribs = get_1d_attr(element);
    attribs["id"] = id_str;

    std::string d;
    for (const auto& c : cmds) {
        if (!d.empty()) d += " ";
        d += c;
    }
    attribs["d"] = d;
    attribs["fill"] = "none";

    if (polar && element.attribute("fill")) {
        attribs["fill"] = element.attribute("fill").value();
    }

    XmlNode path = parent.append_child("path");
    add_attr(path, attribs);
    diagram.register_svg_element(element, path);

    // Arrows
    std::string forward = "marker-end";
    std::string backward = "marker-start";
    if (get_attr(element, "reverse", "no") == "yes") std::swap(forward, backward);

    std::string aw = get_attr(element, "arrow-width", "");
    std::string aa = get_attr(element, "arrow-angles", "");
    if (arrows > 0) add_arrowhead_to_path(diagram, forward, path, aw, aa);
    if (arrows > 1) add_arrowhead_to_path(diagram, backward, path, aw, aa);

    // Clip to bounding box by default
    if (!element.attribute("cliptobbox")) {
        element.append_attribute("cliptobbox").set_value("yes");
    }
    cliptobbox(path, element, diagram);

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, path, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, path, parent);
        finish_outline_graph(element, diagram, parent);
    }
    // else: path already appended to parent
}

static void finish_outline_graph(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

static std::vector<std::string> cartesian_path(XmlNode element, Diagram& diagram,
                                                const MathFunction& f,
                                                const std::array<double, 2>& domain, int N) {
    auto scales = diagram.get_scales();
    BBox bbox = diagram.bbox();

    // Generate x positions
    std::vector<double> x_positions;
    x_positions.reserve(N + 1);
    if (scales[0] == "log") {
        double log_start = std::log10(domain[0]);
        double log_end = std::log10(domain[1]);
        for (int i = 0; i <= N; ++i) {
            double t = log_start + (log_end - log_start) * i / N;
            x_positions.push_back(std::pow(10.0, t));
        }
    } else {
        for (int i = 0; i <= N; ++i) {
            x_positions.push_back(domain[0] + (domain[1] - domain[0]) * i / N);
        }
    }

    // Vertical buffer: 3x height centered on viewing box
    double upper, lower;
    if (scales[1] == "log") {
        double bottom = std::log10(bbox[1]);
        double top = std::log10(bbox[3]);
        lower = std::pow(10.0, bottom - 3.0);
        upper = std::pow(10.0, top + 3.0);
    } else {
        double height = bbox[3] - bbox[1];
        upper = bbox[3] + height;
        lower = bbox[1] - height;
    }

    std::vector<std::string> cmds;
    std::string next_cmd = "M";
    bool last_visible = false;

    for (int i = 0; i <= N; ++i) {
        double x = x_positions[i];
        double dx = (i > 0) ? (x - x_positions[i - 1]) : 0.0;

        // Try to evaluate f(x)
        double y;
        bool eval_ok = true;
        try {
            Value result = f(Value(x));
            y = result.to_double();
            if (std::isnan(y) || std::isinf(y)) eval_ok = false;
        } catch (...) {
            eval_ok = false;
        }

        if (!eval_ok) {
            if (last_visible) {
                // Subdivide to find the singularity
                double ddx = dx / 2.0;
                double xx = x - ddx;
                double last_good_x = x - dx;
                for (int j = 0; j < 8; ++j) {
                    ddx /= 2.0;
                    try {
                        Value rv = f(Value(xx));
                        double yy = rv.to_double();
                        if (std::isnan(yy) || std::isinf(yy)) {
                            xx -= ddx;
                            continue;
                        }
                        last_good_x = xx;
                        xx += ddx;
                    } catch (...) {
                        xx -= ddx;
                    }
                }
                try {
                    double fy = f(Value(last_good_x)).to_double();
                    Point2d p = diagram.transform(Point2d(last_good_x, fy));
                    cmds.push_back("L");
                    cmds.push_back(pt2str(p));
                } catch (...) {}
            }

            last_visible = false;
            next_cmd = "M";
            continue;
        }

        if (y > upper || y < lower) {
            if (last_visible) {
                // Subdivide to find asymptote approach
                double ddx = dx / 2.0;
                double xx = x - ddx;
                double last_good_x = x - dx;
                for (int j = 0; j < 8; ++j) {
                    ddx /= 2.0;
                    try {
                        double yy = f(Value(xx)).to_double();
                        if (yy > upper || yy < lower || std::isnan(yy)) {
                            xx -= ddx;
                        } else {
                            last_good_x = xx;
                            xx += ddx;
                        }
                    } catch (...) {
                        xx -= ddx;
                    }
                }
                try {
                    double fy = f(Value(last_good_x)).to_double();
                    Point2d p = diagram.transform(Point2d(last_good_x, fy));
                    cmds.push_back("L");
                    cmds.push_back(pt2str(p));
                } catch (...) {}
            }

            last_visible = false;
            next_cmd = "M";
            continue;
        }

        // Current point is valid and in range
        if (next_cmd == "M" && x > domain[0]) {
            // Back up to find entry point from asymptote/out-of-bounds
            double ddx = dx / 2.0;
            double xx = x - ddx;
            double last_good_x = x;
            for (int j = 0; j < 8; ++j) {
                ddx /= 2.0;
                try {
                    double yy = f(Value(xx)).to_double();
                    if (yy > upper || yy < lower || std::isnan(yy) || std::isinf(yy)) {
                        xx += ddx;
                        continue;
                    }
                    last_good_x = xx;
                    xx -= ddx;
                } catch (...) {
                    xx += ddx;
                }
            }

            if (last_good_x < x) {
                try {
                    double fy = f(Value(last_good_x)).to_double();
                    Point2d p = diagram.transform(Point2d(last_good_x, fy));
                    cmds.push_back("M");
                    cmds.push_back(pt2str(p));
                    next_cmd = "L";
                } catch (...) {}
            }
        }

        Point2d p = diagram.transform(Point2d(x, y));
        cmds.push_back(next_cmd);
        cmds.push_back(pt2str(p));
        next_cmd = "L";

        last_visible = (y < bbox[3] && y > bbox[1]);
    }

    return cmds;
}

static std::vector<std::string> polar_path(XmlNode element, Diagram& diagram,
                                            const MathFunction& f,
                                            const std::array<double, 2>& domain, int N) {
    BBox bbox = diagram.bbox();
    Eigen::VectorXd bbox_min(2), bbox_max(2);
    bbox_min << bbox[0], bbox[1];
    bbox_max << bbox[2], bbox[3];
    Eigen::VectorXd center_v = midpoint(bbox_min, bbox_max);
    double R = distance(center_v, bbox_max);
    Point2d center_pt(center_v[0], center_v[1]);

    bool domain_degrees = get_attr(element, "domain-degrees", "no") == "yes";
    std::array<double, 2> dom = domain;
    if (domain_degrees) {
        dom[0] = dom[0] * M_PI / 180.0;
        dom[1] = dom[1] * M_PI / 180.0;
    }

    double t = dom[0];
    double dt = (dom[1] - dom[0]) / N;
    std::vector<std::string> polar_cmds;
    std::string next_cmd = "M";

    for (int i = 0; i <= N; ++i) {
        double r;
        try {
            r = f(Value(t)).to_double();
        } catch (...) {
            next_cmd = "M";
            t += dt;
            continue;
        }

        Point2d p(r * std::cos(t), r * std::sin(t));
        Eigen::VectorXd pv(2);
        pv << p[0], p[1];
        Eigen::VectorXd cv(2);
        cv << center_pt[0], center_pt[1];
        if (distance(pv, cv) > 2.0 * R) {
            next_cmd = "M";
            t += dt;
            continue;
        }

        polar_cmds.push_back(next_cmd);
        polar_cmds.push_back(pt2str(diagram.transform(p)));
        next_cmd = "L";
        t += dt;
    }

    if (get_attr(element, "closed", "no") == "yes") {
        polar_cmds.push_back("Z");
    }

    return polar_cmds;
}

}  // namespace prefigure
