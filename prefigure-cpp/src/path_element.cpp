#include "prefigure/path_element.hpp"
#include "prefigure/arrow.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/tags.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <random>
#include <set>
#include <string>
#include <vector>

namespace prefigure {

// Tags that can appear within a path
static const std::set<std::string> path_tags_set = {
    "moveto", "rmoveto", "lineto", "rlineto",
    "horizontal", "vertical",
    "cubic-bezier", "quadratic-bezier",
    "smooth-cubic", "smooth-quadratic"
};

// Tags that are graphical elements embeddable in a path
static const std::set<std::string> graphical_tags = {
    "graph", "parametric-curve", "polygon", "spline"
};

bool is_path_tag(const std::string& tag) {
    return path_tags_set.count(tag) > 0;
}

// Forward declarations
static std::pair<std::vector<std::string>, Point2d> process_tag(
    XmlNode child, Diagram& diagram,
    std::vector<std::string> cmds, Point2d current_point);

static std::pair<std::vector<std::string>, Point2d> decorate(
    XmlNode child, Diagram& diagram,
    Point2d current_point, std::vector<std::string> cmds);

static void finish_outline_path(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

void path_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_path(element, diagram, parent);
        return;
    }

    // Set default attributes
    if (diagram.output_format() == OutputFormat::Tactile) {
        if (element.attribute("stroke")) {
            element.attribute("stroke").set_value("black");
        }
        if (element.attribute("fill")) {
            element.attribute("fill").set_value("lightgray");
        }
    }
    set_attr(element, "stroke", "none");
    set_attr(element, "fill", "none");
    set_attr(element, "thickness", "2");

    // Parse start point
    auto start_attr = element.attribute("start");
    if (!start_attr) {
        spdlog::error("A <path> element needs a @start attribute");
        return;
    }

    Point2d user_start;
    try {
        user_start = diagram.expr_ctx().eval(start_attr.value()).as_point();
    } catch (...) {
        spdlog::error("Error in <path> defining start={}", start_attr.value());
        return;
    }

    Point2d current_point = user_start;
    Point2d start = diagram.transform(user_start);

    std::vector<std::string> cmds;
    cmds.push_back("M");
    cmds.push_back(pt2str(start));

    // Process child sub-elements
    try {
        for (auto child = element.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) continue;
            spdlog::debug("Processing {} inside <path>", child.name());
            auto result = process_tag(child, diagram, std::move(cmds), current_point);
            cmds = std::move(result.first);
            current_point = result.second;
        }
    } catch (const std::exception& e) {
        spdlog::error("Error in <path> processing subelements: {}", e.what());
        return;
    } catch (...) {
        spdlog::error("Error in <path> processing subelements");
        return;
    }

    if (get_attr(element, "closed", "no") == "yes") {
        cmds.push_back("Z");
    }

    std::string d;
    for (const auto& c : cmds) {
        if (!d.empty()) d += " ";
        d += c;
    }

    XmlNode path = diagram.get_scratch().append_child("path");
    diagram.add_id(path, get_attr(element, "id", ""));
    diagram.register_svg_element(element, path);
    path.append_attribute("d").set_value(d.c_str());
    add_attr(path, get_2d_attr(element));

    // Clip to bounding box
    if (!element.attribute("cliptobbox")) {
        element.append_attribute("cliptobbox").set_value("yes");
    }
    cliptobbox(path, element, diagram);

    // Arrows
    int arrows = std::stoi(get_attr(element, "arrows", "0"));
    std::string forward = "marker-end";
    std::string backward = "marker-start";
    if (get_attr(element, "reverse", "no") == "yes") {
        std::swap(forward, backward);
    }

    std::string aw = get_attr(element, "arrow-width", "");
    std::string aa = get_attr(element, "arrow-angles", "");
    if (arrows > 0) {
        add_arrowhead_to_path(diagram, forward, path, aw, aa);
    }
    if (arrows > 1) {
        add_arrowhead_to_path(diagram, backward, path, aw, aa);
    }

    if (get_attr(element, "mid-arrow", "no") == "yes") {
        add_arrowhead_to_path(diagram, "marker-mid", path);
    }

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, path, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, path, parent);
        finish_outline_path(element, diagram, parent);
    } else {
        parent.append_copy(path);
    }
}

// Helper to parse distance/heading into a user-space displacement
static Point2d parse_distance_heading(XmlNode child, Diagram& diagram) {
    double dist = diagram.expr_ctx().eval(child.attribute("distance").value()).to_double();
    double heading = 0.0;
    if (child.attribute("heading")) {
        heading = diagram.expr_ctx().eval(child.attribute("heading").value()).to_double();
    }
    if (get_attr(child, "degrees", "yes") == "yes") {
        heading = heading * M_PI / 180.0;
    }
    return Point2d(dist * std::cos(heading), dist * std::sin(heading));
}

static std::pair<std::vector<std::string>, Point2d> process_tag(
    XmlNode child, Diagram& diagram,
    std::vector<std::string> cmds, Point2d current_point)
{
    std::string tag = child.name();

    if (tag == "moveto") {
        Point2d user_point;
        if (child.attribute("distance")) {
            user_point = parse_distance_heading(child, diagram);
        } else {
            try {
                user_point = diagram.expr_ctx().eval(
                    child.attribute("point").value()).as_point();
            } catch (...) {
                spdlog::error("Error in <moveto> defining point={}",
                              get_attr(child, "point", ""));
                return {cmds, current_point};
            }
        }
        Point2d point = diagram.transform(user_point);
        cmds.push_back("M");
        cmds.push_back(pt2str(point));
        current_point = user_point;
        return {cmds, current_point};
    }

    if (tag == "rmoveto") {
        Point2d displacement;
        if (child.attribute("distance")) {
            displacement = parse_distance_heading(child, diagram);
        } else {
            try {
                displacement = diagram.expr_ctx().eval(
                    child.attribute("point").value()).as_point();
            } catch (...) {
                spdlog::error("Error in <rmoveto> defining point={}",
                              get_attr(child, "point", ""));
                return {cmds, current_point};
            }
        }
        current_point = current_point + displacement;
        Point2d point = diagram.transform(current_point);
        cmds.push_back("M");
        cmds.push_back(pt2str(point));
        return {cmds, current_point};
    }

    if (tag == "horizontal") {
        double dist;
        try {
            dist = diagram.expr_ctx().eval(child.attribute("distance").value()).to_double();
        } catch (...) {
            spdlog::error("Error in <horizontal> defining distance={}",
                          get_attr(child, "distance", ""));
            return {cmds, current_point};
        }
        Point2d user_point(current_point[0] + dist, current_point[1]);
        // Convert to lineto
        child.set_name("lineto");
        std::string pt_str = pt2long_str(user_point, ",");
        child.attribute("point") ?
            child.attribute("point").set_value(pt_str.c_str()) :
            child.append_attribute("point").set_value(pt_str.c_str());
        tag = "lineto";
    }

    if (tag == "vertical") {
        double dist;
        try {
            dist = diagram.expr_ctx().eval(child.attribute("distance").value()).to_double();
        } catch (...) {
            spdlog::error("Error in <vertical> defining distance={}",
                          get_attr(child, "distance", ""));
            return {cmds, current_point};
        }
        Point2d user_point(current_point[0], current_point[1] + dist);
        child.set_name("lineto");
        std::string pt_str = pt2long_str(user_point, ",");
        child.attribute("point") ?
            child.attribute("point").set_value(pt_str.c_str()) :
            child.append_attribute("point").set_value(pt_str.c_str());
        tag = "lineto";
    }

    if (tag == "rlineto") {
        Point2d displacement;
        if (child.attribute("distance") && !child.attribute("point")) {
            displacement = parse_distance_heading(child, diagram);
        } else {
            try {
                displacement = diagram.expr_ctx().eval(
                    child.attribute("point").value()).as_point();
            } catch (...) {
                spdlog::error("Error in <rlineto> defining point={}",
                              get_attr(child, "point", ""));
                return {cmds, current_point};
            }
        }
        Point2d user_point = current_point + displacement;
        child.set_name("lineto");
        std::string pt_str = pt2long_str(user_point, ",");
        child.attribute("point") ?
            child.attribute("point").set_value(pt_str.c_str()) :
            child.append_attribute("point").set_value(pt_str.c_str());
        tag = "lineto";
    }

    if (tag == "lineto") {
        // Handle distance/heading form without explicit point
        if (!child.attribute("point") && child.attribute("distance")) {
            Point2d disp = parse_distance_heading(child, diagram);
            std::string pt_str = pt2long_str(disp, ",");
            child.append_attribute("point").set_value(pt_str.c_str());
        }

        // Decoration?
        if (child.attribute("decoration")) {
            return decorate(child, diagram, current_point, std::move(cmds));
        }

        Point2d user_point;
        try {
            user_point = diagram.expr_ctx().eval(
                child.attribute("point").value()).as_point();
        } catch (...) {
            spdlog::error("Error in <lineto> defining point={}",
                          get_attr(child, "point", ""));
            return {cmds, current_point};
        }
        Point2d point = diagram.transform(user_point);
        cmds.push_back("L");
        cmds.push_back(pt2str(point));
        current_point = user_point;
        return {cmds, current_point};
    }

    if (tag == "cubic-bezier") {
        cmds.push_back("C");
        try {
            auto val = diagram.expr_ctx().eval(child.attribute("controls").value());
            auto& v = val.as_vector();
            // v should contain pairs of coords: [x1,y1,x2,y2,x3,y3]
            int num_points = static_cast<int>(v.size()) / 2;
            std::string pts_str;
            for (int i = 0; i < num_points; ++i) {
                Point2d cp(v[i * 2], v[i * 2 + 1]);
                Point2d tcp = diagram.transform(cp);
                if (!pts_str.empty()) pts_str += " ";
                pts_str += pt2str(tcp);
            }
            cmds.push_back(pts_str);
            current_point = Point2d(v[v.size() - 2], v[v.size() - 1]);
        } catch (...) {
            spdlog::error("Error in <cubic-bezier> defining controls={}",
                          get_attr(child, "controls", ""));
        }
        return {cmds, current_point};
    }

    if (tag == "quadratic-bezier") {
        cmds.push_back("Q");
        try {
            auto val = diagram.expr_ctx().eval(child.attribute("controls").value());
            auto& v = val.as_vector();
            int num_points = static_cast<int>(v.size()) / 2;
            std::string pts_str;
            for (int i = 0; i < num_points; ++i) {
                Point2d cp(v[i * 2], v[i * 2 + 1]);
                Point2d tcp = diagram.transform(cp);
                if (!pts_str.empty()) pts_str += " ";
                pts_str += pt2str(tcp);
            }
            cmds.push_back(pts_str);
            current_point = Point2d(v[v.size() - 2], v[v.size() - 1]);
        } catch (...) {
            spdlog::error("Error in <quadratic-bezier> defining controls={}",
                          get_attr(child, "controls", ""));
        }
        return {cmds, current_point};
    }

    if (tag == "arc") {
        try {
            auto center = diagram.expr_ctx().eval(
                child.attribute("center").value()).as_point();
            double radius = diagram.expr_ctx().eval(
                child.attribute("radius").value()).to_double();
            auto range_val = diagram.expr_ctx().eval(
                child.attribute("range").value()).as_vector();
            std::array<double, 2> angular_range = {range_val[0], range_val[1]};

            if (get_attr(child, "degrees", "yes") == "yes") {
                angular_range[0] = angular_range[0] * M_PI / 180.0;
                angular_range[1] = angular_range[1] * M_PI / 180.0;
            }

            int N = 100;
            double t = angular_range[0];
            double dt = (angular_range[1] - angular_range[0]) / N;

            Point2d user_start(center[0] + radius * std::cos(t),
                              center[1] + radius * std::sin(t));
            Point2d start_pt = diagram.transform(user_start);
            cmds.push_back("L");
            cmds.push_back(pt2str(start_pt));

            for (int i = 0; i < N; ++i) {
                t += dt;
                Point2d user_pt(center[0] + radius * std::cos(t),
                               center[1] + radius * std::sin(t));
                Point2d pt = diagram.transform(user_pt);
                cmds.push_back("L");
                cmds.push_back(pt2str(pt));
            }
        } catch (...) {
            spdlog::error("Error in <arc> defining data: @center, @radius, or @range");
        }
        return {cmds, current_point};
    }

    if (tag == "repeat") {
        try {
            std::string parameter = child.attribute("parameter").value();
            auto eq_pos = parameter.find('=');
            std::string var = parameter.substr(0, eq_pos);
            // Trim
            auto trim = [](std::string& s) {
                auto start = s.find_first_not_of(" \t");
                auto end = s.find_last_not_of(" \t");
                if (start != std::string::npos) s = s.substr(start, end - start + 1);
            };
            trim(var);

            std::string expr = parameter.substr(eq_pos + 1);
            auto dot_pos = expr.find("..");
            std::string start_str = expr.substr(0, dot_pos);
            std::string stop_str = expr.substr(dot_pos + 2);
            trim(start_str);
            trim(stop_str);

            int start_val = static_cast<int>(diagram.expr_ctx().eval(start_str).to_double());
            int stop_val = static_cast<int>(diagram.expr_ctx().eval(stop_str).to_double());

            for (int k = start_val; k <= stop_val; ++k) {
                std::string k_str = std::to_string(k);
                diagram.expr_ctx().eval(k_str, var);

                for (auto sub_child = child.first_child(); sub_child;
                     sub_child = sub_child.next_sibling()) {
                    if (sub_child.type() != pugi::node_element) continue;
                    auto result = process_tag(sub_child, diagram,
                                             std::move(cmds), current_point);
                    cmds = std::move(result.first);
                    current_point = result.second;
                }
            }
        } catch (...) {
            spdlog::error("Error in <repeat> defining parameter={}",
                          get_attr(child, "parameter", ""));
        }
        return {cmds, current_point};
    }

    // Graphical tags (graph, parametric-curve, polygon, spline)
    if (graphical_tags.count(tag) > 0) {
        // Create a dummy parent, parse the element, extract the path data
        auto dummy_parent = diagram.get_scratch().append_child("group");
        tags::parse_element(child, diagram, dummy_parent, OutlineStatus::None);

        // Get the first child's 'd' attribute
        auto first_child = dummy_parent.first_child();
        if (first_child && first_child.attribute("d")) {
            std::string child_cmds = first_child.attribute("d").value();
            // Trim
            auto start_pos = child_cmds.find_first_not_of(" \t\n\r");
            if (start_pos != std::string::npos) {
                child_cmds = child_cmds.substr(start_pos);
            }
            auto end_pos = child_cmds.find_last_not_of(" \t\n\r");
            if (end_pos != std::string::npos) {
                child_cmds = child_cmds.substr(0, end_pos + 1);
            }

            // Replace leading M with L
            if (!child_cmds.empty() && child_cmds[0] == 'M') {
                child_cmds[0] = 'L';
            }
            // Remove trailing Z
            if (!child_cmds.empty() && child_cmds.back() == 'Z') {
                child_cmds.pop_back();
                // Trim trailing whitespace
                end_pos = child_cmds.find_last_not_of(" \t\n\r");
                if (end_pos != std::string::npos) {
                    child_cmds = child_cmds.substr(0, end_pos + 1);
                }
            }

            cmds.push_back(child_cmds);

            // Find the final point to update current_point
            // Split by whitespace and take last two numbers
            std::istringstream iss(child_cmds);
            std::vector<std::string> tokens;
            std::string tok;
            while (iss >> tok) tokens.push_back(tok);

            if (tokens.size() >= 2) {
                try {
                    double fx = std::stod(tokens[tokens.size() - 2]);
                    double fy = std::stod(tokens[tokens.size() - 1]);
                    current_point = diagram.inverse_transform(Point2d(fx, fy));
                } catch (...) {}
            }
        }

        // Clean up dummy parent
        dummy_parent.parent().remove_child(dummy_parent);
        return {cmds, current_point};
    }

    spdlog::warn("Unknown tag in <path>: {}", tag);
    return {cmds, current_point};
}

static std::pair<std::vector<std::string>, Point2d> decorate(
    XmlNode child, Diagram& diagram,
    Point2d current_point, std::vector<std::string> cmds)
{
    Point2d user_point = diagram.expr_ctx().eval(
        child.attribute("point").value()).as_point();

    CTM ctm;
    Point2d p0 = diagram.transform(current_point);
    Point2d p1 = diagram.transform(user_point);
    Point2d diff = p1 - p0;
    double len = length(Eigen::VectorXd(diff));
    ctm.translate(p0[0], p0[1]);
    ctm.rotate(std::atan2(diff[1], diff[0]), "rad");

    std::string decoration = child.attribute("decoration").value();
    // Parse "type; key=val; key=val; ..."
    std::vector<std::string> decoration_data;
    {
        std::istringstream iss(decoration);
        std::string tok;
        while (std::getline(iss, tok, ';')) {
            auto s = tok.find_first_not_of(" \t");
            auto e = tok.find_last_not_of(" \t");
            if (s != std::string::npos) {
                decoration_data.push_back(tok.substr(s, e - s + 1));
            }
        }
    }

    std::string dec_type = decoration_data.empty() ? "" : decoration_data[0];

    // Parse key=value pairs
    std::unordered_map<std::string, std::string> data;
    for (size_t i = 1; i < decoration_data.size(); ++i) {
        auto eq = decoration_data[i].find('=');
        if (eq != std::string::npos) {
            std::string key = decoration_data[i].substr(0, eq);
            std::string val = decoration_data[i].substr(eq + 1);
            auto trim = [](std::string& s) {
                auto start = s.find_first_not_of(" \t");
                auto end = s.find_last_not_of(" \t");
                if (start != std::string::npos) s = s.substr(start, end - start + 1);
            };
            trim(key);
            trim(val);
            data[key] = val;
        }
    }

    auto eval_or_default = [&](const std::string& key, const std::string& def) -> double {
        auto it = data.find(key);
        if (it != data.end()) return diagram.expr_ctx().eval(it->second).to_double();
        return diagram.expr_ctx().eval(def).to_double();
    };

    auto eval_point_or_default = [&](const std::string& key, const std::string& def) -> Point2d {
        auto it = data.find(key);
        if (it != data.end()) return diagram.expr_ctx().eval(it->second).as_point();
        return diagram.expr_ctx().eval(def).as_point();
    };

    if (dec_type == "coil") {
        try {
            Point2d dimensions = eval_point_or_default("dimensions", "(10,5)");
            double location = eval_or_default("center", "0.5");
            int number;
            if (data.count("number")) {
                number = static_cast<int>(diagram.expr_ctx().eval(data["number"]).to_double());
            } else {
                number = static_cast<int>(std::floor((len - dimensions[0] / 2.0) / dimensions[0]));
            }

            double half_coil_fraction = (number + 0.5) * dimensions[0] / len;
            while (location - half_coil_fraction < 0 || location + half_coil_fraction > 1) {
                number--;
                half_coil_fraction = (number + 0.5) * dimensions[0] / len;
            }
            double start_coil = len * (location - half_coil_fraction);
            double coil_length = 2.0 * half_coil_fraction * len;

            int N_coil = 40;
            double dt = 2.0 * M_PI / N_coil;
            double t = 0;
            double x_init = start_coil;
            double x_pos = x_init + dimensions[0] / 2.0;
            int iterates = static_cast<int>(std::floor((number + 0.5) * N_coil));
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(x_init, 0))));
            double dx_coil = (coil_length - dimensions[0]) / iterates;
            double x_val = 0, y_val = 0;
            for (int i = 0; i < iterates; ++i) {
                y_val = -dimensions[1] * std::sin(t);
                x_pos += dx_coil;
                x_val = x_pos - dimensions[0] / 2.0 * std::cos(t);
                t += dt;
                cmds.push_back("L");
                cmds.push_back(pt2str(ctm.transform(Point2d(x_val, y_val))));
            }
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(x_val, 0))));
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(len, 0))));
        } catch (...) {
            spdlog::error("Error processing decoration data for a coil");
        }
    }

    if (dec_type == "zigzag") {
        try {
            Point2d dimensions = eval_point_or_default("dimensions", "(10,5)");
            double location = eval_or_default("center", "0.5");
            int number;
            if (data.count("number")) {
                number = static_cast<int>(diagram.expr_ctx().eval(data["number"]).to_double());
            } else {
                number = static_cast<int>(std::floor((len - dimensions[0] / 2.0) / dimensions[0]));
            }

            double half_zig_fraction = number * dimensions[0] / len;
            while (location - half_zig_fraction < 0 || location + half_zig_fraction > 1) {
                number--;
                half_zig_fraction = number * dimensions[0] / len;
            }
            double start_zig = len * (location - half_zig_fraction);
            double zig_length = 2.0 * half_zig_fraction * len;

            int N_zig = 4;
            double dt_zig = 2.0 * M_PI / N_zig;
            double t = 0;
            double x_pos = start_zig;
            int iterates = static_cast<int>(std::floor(number * N_zig));
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(x_pos, 0))));
            double dx_zig = zig_length / iterates;
            double y_val = 0;
            for (int i = 0; i < iterates; ++i) {
                t += dt_zig;
                x_pos += dx_zig;
                y_val = -dimensions[1] * std::sin(t);
                cmds.push_back("L");
                cmds.push_back(pt2str(ctm.transform(Point2d(x_pos, y_val))));
            }
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(x_pos, 0))));
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(len, 0))));
        } catch (...) {
            spdlog::error("Error processing zigzag decoration data");
        }
    }

    if (dec_type == "wave") {
        try {
            Point2d dimensions = eval_point_or_default("dimensions", "(10,5)");
            double location = eval_or_default("center", "0.5");
            int number;
            if (data.count("number")) {
                number = static_cast<int>(diagram.expr_ctx().eval(data["number"]).to_double());
            } else {
                number = static_cast<int>(std::floor((len - dimensions[0] / 2.0) / dimensions[0]));
            }

            double half_wave_fraction = number * dimensions[0] / len;
            while (location - half_wave_fraction < 0 || location + half_wave_fraction > 1) {
                number--;
                half_wave_fraction = number * dimensions[0] / len;
            }
            double start_wave = len * (location - half_wave_fraction);
            double wave_length = 2.0 * half_wave_fraction * len;

            int N_wave = 30;
            double dt_wave = 2.0 * M_PI / N_wave;
            double t = 0;
            double x_pos = start_wave;
            int iterates = static_cast<int>(std::floor(number * N_wave));
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(x_pos, 0))));
            double dx_wave = wave_length / iterates;
            double y_val = 0;
            for (int i = 0; i < iterates; ++i) {
                t += dt_wave;
                x_pos += dx_wave;
                y_val = -dimensions[1] * std::sin(t);
                cmds.push_back("L");
                cmds.push_back(pt2str(ctm.transform(Point2d(x_pos, y_val))));
            }
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(x_pos, 0))));
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(len, 0))));
        } catch (...) {
            spdlog::error("Error in wave decoration data");
        }
    }

    if (dec_type == "ragged") {
        try {
            double offset = diagram.expr_ctx().eval(data.at("offset")).to_double();
            double step = diagram.expr_ctx().eval(data.at("step")).to_double();

            int seed = 1;
            if (data.count("seed")) {
                seed = static_cast<int>(diagram.expr_ctx().eval(data["seed"]).to_double());
            }

            std::mt19937 rng(seed);
            std::uniform_real_distribution<double> dist(0.0, 1.0);

            double x_pos = 0;
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(0, 0))));
            while (x_pos < len - step) {
                double y_pos = 2.0 * offset * (dist(rng) - 0.5);
                x_pos += (0.5 * dist(rng) + 0.75) * step;
                cmds.push_back("L");
                cmds.push_back(pt2str(ctm.transform(Point2d(x_pos, y_pos))));
            }
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(len, 0))));
        } catch (...) {
            spdlog::error("Error in ragged decoration data");
        }
    }

    if (dec_type == "capacitor") {
        try {
            Point2d dimensions = eval_point_or_default("dimensions", "(10,5)");
            double location = eval_or_default("center", "0.5");

            double x_mid = len * location;
            double x0 = x_mid - dimensions[0] / 2.0;
            double x1 = x_mid + dimensions[0] / 2.0;

            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(x0, 0))));
            cmds.push_back("M");
            cmds.push_back(pt2str(ctm.transform(Point2d(x0, dimensions[1]))));
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(x0, -dimensions[1]))));
            cmds.push_back("M");
            cmds.push_back(pt2str(ctm.transform(Point2d(x1, dimensions[1]))));
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(x1, -dimensions[1]))));
            cmds.push_back("M");
            cmds.push_back(pt2str(ctm.transform(Point2d(x1, 0))));
            cmds.push_back("L");
            cmds.push_back(pt2str(ctm.transform(Point2d(len, 0))));
        } catch (...) {
            spdlog::error("Error in capacitor decoration data");
        }
    }

    return {cmds, user_point};
}

}  // namespace prefigure
