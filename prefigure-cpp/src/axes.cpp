#include "prefigure/axes.hpp"
#include "prefigure/arrow.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/label.hpp"
#include "prefigure/line.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace prefigure {

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

static AxesState* g_axes_state = nullptr;

AxesState* get_axes_state() {
    return g_axes_state;
}

// ---------------------------------------------------------------------------
// find_label_positions
// ---------------------------------------------------------------------------

std::array<double, 3> find_label_positions(
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

    auto& delta = label_delta_map();
    int key = static_cast<int>(std::round(2.0 * distance));
    auto it = delta.find(key);
    double mult = (it != delta.end()) ? it->second : 1.0;

    if (dx > 1.0) {
        dx *= mult;
        dx = std::round(dx);  // int(dx) in Python
    } else {
        dx *= mult;
    }

    double x0, x1;
    if (r1 < r0) {
        dx *= -1.0;
        x0 = dx * std::floor(r0 / dx + 1e-10);
        x1 = dx * std::ceil(r1 / dx - 1e-10);
    } else {
        x0 = dx * std::ceil(r0 / dx - 1e-10);
        x1 = dx * std::floor(r1 / dx + 1e-10);
    }
    return {x0, dx, x1};
}

// ---------------------------------------------------------------------------
// find_log_positions (axes version)
// ---------------------------------------------------------------------------

std::vector<double> find_log_positions(const std::vector<double>& r) {
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
        if (width < 1.5)       spacing = 2.0;
        else if (width <= 10)  spacing = 1.0;
        else                   spacing = 5.0 / width;
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
// get_pi_text
// ---------------------------------------------------------------------------

std::string get_pi_text(double x) {
    if (std::abs(std::abs(x) - 1.0) < 1e-10) {
        return x < 0 ? R"(-\pi)" : R"(\pi)";
    }
    if (std::abs(x - std::round(x)) < 1e-10) {
        return std::format("{}", static_cast<int>(std::round(x))) + R"(\pi)";
    }

    // Check quarters
    if (std::abs(4.0 * x - std::round(4.0 * x)) < 1e-10) {
        int num = static_cast<int>(std::round(4.0 * x));
        if (num == -1) return R"(-\frac{\pi}{4})";
        if (num == 1)  return R"(\frac{\pi}{4})";
        if (num % 2 == 1) {
            if (num < 0)
                return std::format(R"(-\frac{{{}\pi}}{{4}})", -num);
            else
                return std::format(R"(\frac{{{}\pi}}{{4}})", num);
        }
    }
    // Check halves
    if (std::abs(2.0 * x - std::round(2.0 * x)) < 1e-10) {
        int num = static_cast<int>(std::round(2.0 * x));
        if (num == -1) return R"(-\frac{\pi}{2})";
        if (num == 1)  return R"(\frac{\pi}{2})";
        if (num < 0)
            return std::format(R"(-\frac{{{}\pi}}{{2}})", -num);
        else
            return std::format(R"(\frac{{{}\pi}}{{2}})", num);
    }
    // Check thirds
    if (std::abs(3.0 * x - std::round(3.0 * x)) < 1e-10) {
        int num = static_cast<int>(std::round(3.0 * x));
        if (num == -1) return R"(-\frac{\pi}{3})";
        if (num == 1)  return R"(\frac{\pi}{3})";
        if (num < 0)
            return std::format(R"(-\frac{{{}\pi}}{{3}})", -num);
        else
            return std::format(R"(\frac{{{}\pi}}{{3}})", num);
    }
    // Check sixths
    if (std::abs(6.0 * x - std::round(6.0 * x)) < 1e-10) {
        int num = static_cast<int>(std::round(6.0 * x));
        if (num == -1) return R"(-\frac{\pi}{6})";
        if (num == 1)  return R"(\frac{\pi}{6})";
        if (num < 0)
            return std::format(R"(-\frac{{{}\pi}}{{6}})", -num);
        else
            return std::format(R"(\frac{{{}\pi}}{{6}})", num);
    }

    return std::format("{:g}\\pi", x);
}

// ---------------------------------------------------------------------------
// label_text
// ---------------------------------------------------------------------------

std::string label_text(double x, bool commas, Diagram& diagram) {
    std::string prefix;
    if (x < 0) {
        prefix = "-";
        x = std::abs(x);
    }
    std::string text = std::format("{:g}", x);

    // Handle exponential notation
    if (text.find('e') != std::string::npos) {
        long long integer = static_cast<long long>(std::floor(x));
        double fraction = x - integer;
        std::string suffix;
        if (fraction > 1e-14) {
            std::string frac_str = std::format("{:g}", fraction);
            // skip leading '0'
            if (!frac_str.empty() && frac_str[0] == '0') {
                suffix = frac_str.substr(1);
            } else {
                suffix = frac_str;
            }
        }
        std::string int_part;
        while (integer >= 10) {
            int_part = std::format("{}", integer % 10) + int_part;
            integer /= 10;
        }
        int_part = std::format("{}", integer) + int_part;
        text = int_part + suffix;
    }

    if (!commas) {
        return R"(\text{)" + prefix + text + "}";
    }

    // Add comma separators
    auto period = text.find('.');
    std::string comma_include = "{,}";
    if (diagram.get_environment() == Environment::Pyodide) {
        comma_include = ",";
    }
    std::string suffix;
    if (period != std::string::npos) {
        suffix = text.substr(period);
        text = text.substr(0, period);
    }
    while (text.size() > 3) {
        suffix = comma_include + text.substr(text.size() - 3) + suffix;
        text = text.substr(0, text.size() - 3);
    }
    text = text + suffix;
    return R"(\text{)" + prefix + text + "}";
}

// ---------------------------------------------------------------------------
// Internal helpers for Axes construction
// ---------------------------------------------------------------------------

static void position_axes(AxesState& state, XmlNode element, Diagram& diagram) {
    auto scales = diagram.get_scales();

    // Horizontal axis positioning
    state.y_axis_location = 0;
    state.y_axis_offsets = {0, 0};
    state.h_zero_include = false;
    state.top_labels = false;

    if (state.bbox[1] * state.bbox[3] >= 0) {
        if (state.bbox[3] <= 0) {
            state.top_labels = true;
            state.y_axis_location = state.bbox[3];
            if (state.bbox[3] < 0)
                state.y_axis_offsets = {0, -5};
        } else {
            if (std::abs(state.bbox[1]) > 1e-10) {
                state.y_axis_location = state.bbox[1];
                state.y_axis_offsets = {5, 0};
            }
        }
    }

    std::string h_frame = element.attribute("h-frame").as_string("");
    if (h_frame == "bottom") {
        state.y_axis_location = state.bbox[1];
        state.y_axis_offsets = {0, 0};
        state.h_zero_include = true;
    }
    if (h_frame == "top") {
        state.y_axis_location = state.bbox[3];
        state.y_axis_offsets = {0, 0};
        state.h_zero_include = true;
        state.top_labels = true;
    }

    if (scales[1] == "log") {
        state.y_axis_offsets = {0, 0};
        state.h_zero_include = true;
    }

    // h_exclude
    state.h_exclude.clear();
    state.h_zero_label = std::string(element.attribute("h-zero-label").as_string("no")) == "yes";
    if (!state.h_zero_include &&
        state.axes_attribute != "horizontal" &&
        !state.h_zero_label) {
        state.h_exclude.push_back(0.0);
    }

    state.h_tick_direction = 1;
    if (state.top_labels) state.h_tick_direction = -1;

    // Vertical axis positioning
    state.x_axis_location = 0;
    state.x_axis_offsets = {0, 0};
    state.v_zero_include = false;
    state.right_labels = false;

    if (state.bbox[0] * state.bbox[2] >= 0) {
        if (state.bbox[2] <= 0) {
            state.right_labels = true;
            state.x_axis_location = state.bbox[2];
            if (state.bbox[2] < 0)
                state.x_axis_offsets = {0, -10};
        } else {
            if (std::abs(state.bbox[0]) > 1e-10) {
                state.x_axis_location = state.bbox[0];
                state.x_axis_offsets = {10, 0};
            }
        }
    }

    std::string v_frame = element.attribute("v-frame").as_string("");
    if (v_frame == "left") {
        state.x_axis_location = state.bbox[0];
        state.x_axis_offsets = {0, 0};
        state.v_zero_include = true;
    }
    if (v_frame == "right") {
        state.x_axis_location = state.bbox[2];
        state.x_axis_offsets = {0, 0};
        state.v_zero_include = true;
        state.right_labels = true;
    }

    if (scales[1] == "log") {
        state.x_axis_offsets = {0, 0};
        state.v_zero_include = true;
    }

    // v_exclude
    state.v_exclude.clear();
    state.v_zero_label = std::string(element.attribute("v-zero-label").as_string("no")) == "yes";
    if (!state.v_zero_include &&
        state.axes_attribute != "vertical" &&
        !state.v_zero_label) {
        state.v_exclude.push_back(0.0);
    }

    state.v_tick_direction = 1;
    if (state.right_labels) state.v_tick_direction = -1;
}

static void apply_axis_labels(AxesState& state, XmlNode element, Diagram& diagram, XmlNode parent) {
    // Process xlabel attribute
    std::string xlabel_attr = element.attribute("xlabel").as_string("");
    if (!xlabel_attr.empty()) {
        auto scratch = diagram.get_scratch();
        auto el = scratch.append_child("label");
        auto math_el = el.append_child("m");
        math_el.text().set(xlabel_attr.c_str());
        el.append_attribute("clear-background").set_value("no");
        el.append_attribute("p").set_value(
            std::format("({},{})", state.bbox[2], state.y_axis_location).c_str());
        el.append_attribute("alignment").set_value("xl");
        if (state.arrows > 0) {
            if (state.tactile)
                el.append_attribute("offset").set_value("(-6,6)");
            else
                el.append_attribute("offset").set_value("(-2,2)");
        }
        el.attribute("clear-background").set_value(state.clear_background.c_str());
        label_element(el, diagram, parent, OutlineStatus::None);
    }

    // Process ylabel attribute
    std::string ylabel_attr = element.attribute("ylabel").as_string("");
    if (!ylabel_attr.empty()) {
        auto scratch = diagram.get_scratch();
        auto el = scratch.append_child("label");
        auto math_el = el.append_child("m");
        math_el.text().set(ylabel_attr.c_str());
        el.append_attribute("clear-background").set_value("no");
        el.append_attribute("p").set_value(
            std::format("({},{})", state.x_axis_location, state.bbox[3]).c_str());
        el.append_attribute("alignment").set_value("se");
        if (state.arrows > 0)
            el.append_attribute("offset").set_value("(2,-2)");
        el.attribute("clear-background").set_value(state.clear_background.c_str());
        label_element(el, diagram, parent, OutlineStatus::None);
    }

    // Process <xlabel> and <ylabel> child elements
    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        std::string tag = child.name();
        if (tag == "xlabel") {
            child.set_name("label");
            child.append_attribute("user-coords").set_value("no");
            Point2d anchor = diagram.transform(
                Point2d(state.bbox[2], state.y_axis_location));
            child.append_attribute("anchor").set_value(
                pt2str(anchor, ",").c_str());
            if (!child.attribute("alignment")) {
                child.append_attribute("alignment").set_value("east");
            }
            if (!child.attribute("offset")) {
                if (state.arrows > 0)
                    child.append_attribute("offset").set_value("(2,0)");
                else
                    child.append_attribute("offset").set_value("(1,0)");
            }
            label_element(child, diagram, parent, OutlineStatus::None);
            continue;
        }
        if (tag == "ylabel") {
            child.set_name("label");
            child.append_attribute("user-coords").set_value("no");
            Point2d anchor = diagram.transform(
                Point2d(state.x_axis_location, state.bbox[3]));
            child.append_attribute("anchor").set_value(
                pt2str(anchor, ",").c_str());
            if (!child.attribute("alignment")) {
                child.append_attribute("alignment").set_value("north");
                if (!child.attribute("offset")) {
                    if (state.arrows > 0)
                        child.append_attribute("offset").set_value("(0,2)");
                    else
                        child.append_attribute("offset").set_value("(0,1)");
                }
            }
            label_element(child, diagram, parent, OutlineStatus::None);
            continue;
        }
        spdlog::info("{} element is not allowed inside a <label>", tag);
    }
}

static void add_h_axis(AxesState& state, XmlNode element, Diagram& diagram) {
    Point2d left = diagram.transform(Point2d(state.bbox[0], state.y_axis_location));
    Point2d right = diagram.transform(Point2d(state.bbox[2], state.y_axis_location));

    Eigen::VectorXd offsets(2);
    offsets[0] = state.x_axis_offsets[0];
    offsets[1] = state.x_axis_offsets[1];

    XmlNode h_line = mk_line(left, right, diagram, "", offsets, false);
    h_line.append_attribute("stroke").set_value(state.stroke.c_str());
    h_line.append_attribute("stroke-width").set_value(state.thickness.c_str());

    if (state.arrows > 0)
        add_arrowhead_to_path(diagram, "marker-end", h_line);
    if (state.arrows > 1)
        add_arrowhead_to_path(diagram, "marker-start", h_line);

    state.axes_group.append_copy(h_line);
}

static void add_v_axis(AxesState& state, XmlNode element, Diagram& diagram) {
    Point2d bottom = diagram.transform(Point2d(state.x_axis_location, state.bbox[1]));
    Point2d top = diagram.transform(Point2d(state.x_axis_location, state.bbox[3]));

    Eigen::VectorXd offsets(2);
    offsets[0] = state.y_axis_offsets[0];
    offsets[1] = state.y_axis_offsets[1];

    XmlNode v_line = mk_line(bottom, top, diagram, "", offsets, false);
    v_line.append_attribute("stroke").set_value(state.stroke.c_str());
    v_line.append_attribute("stroke-width").set_value(state.thickness.c_str());

    if (state.arrows > 0)
        add_arrowhead_to_path(diagram, "marker-end", v_line);
    if (state.arrows > 1)
        add_arrowhead_to_path(diagram, "marker-start", v_line);

    state.axes_group.append_copy(v_line);
}

static void horizontal_ticks(AxesState& state, XmlNode element, Diagram& diagram) {
    std::string hticks_str = element.attribute("hticks").as_string("");
    if (hticks_str.empty()) return;

    state.axes_group.append_copy(state.h_tick_group);
    // Find the appended copy to work with
    state.h_tick_group_appended = true;

    std::vector<double> x_positions;
    auto scale = diagram.get_scales()[0];

    try {
        Value val = diagram.expr_ctx().eval(hticks_str);
        if (scale == "log") {
            auto& vec = val.as_vector();
            std::vector<double> r(vec.data(), vec.data() + vec.size());
            x_positions = find_log_positions(r);
        } else {
            auto& vec = val.as_vector();
            double start = vec[0], step = vec[1], end = vec[2];
            int N = static_cast<int>(std::round((end - start) / step));
            for (int i = 0; i <= N; ++i) {
                x_positions.push_back(start + i * (end - start) / N);
            }
        }
    } catch (...) {
        spdlog::error("Error in <axes> parsing hticks={}", hticks_str);
        return;
    }

    // Find the actual h_tick_group in axes_group (the copy)
    XmlNode tick_grp = state.axes_group.last_child();

    for (double x : x_positions) {
        if (x < state.bbox[0] || x > state.bbox[2]) continue;

        bool skip = false;
        for (double p : state.h_exclude) {
            double dist;
            if (scale == "log")
                dist = std::abs(std::log10(x) - std::log10(p));
            else
                dist = std::abs(x - p);
            if (dist < state.position_tolerance) { skip = true; break; }
        }
        if (skip) continue;

        Point2d pt = diagram.transform(Point2d(x, state.y_axis_location));
        XmlNode line_el = mk_line(
            Point2d(pt[0], pt[1] + state.h_tick_direction * state.ticksize[0]),
            Point2d(pt[0], pt[1] - state.h_tick_direction * state.ticksize[1]),
            diagram, "", Eigen::VectorXd(), false);
        tick_grp.append_copy(line_el);
    }
}

static void vertical_ticks(AxesState& state, XmlNode element, Diagram& diagram) {
    std::string vticks_str = element.attribute("vticks").as_string("");
    if (vticks_str.empty()) return;

    state.axes_group.append_copy(state.v_tick_group);
    state.v_tick_group_appended = true;

    std::vector<double> y_positions;
    auto scale = diagram.get_scales()[1];

    try {
        Value val = diagram.expr_ctx().eval(vticks_str);
        if (scale == "log") {
            auto& vec = val.as_vector();
            std::vector<double> r(vec.data(), vec.data() + vec.size());
            y_positions = find_log_positions(r);
        } else {
            auto& vec = val.as_vector();
            double start = vec[0], step = vec[1], end = vec[2];
            int N = static_cast<int>(std::round((end - start) / step));
            for (int i = 0; i <= N; ++i) {
                y_positions.push_back(start + i * (end - start) / N);
            }
        }
    } catch (...) {
        spdlog::error("Error in <axes> parsing vticks={}", vticks_str);
        return;
    }

    XmlNode tick_grp = state.axes_group.last_child();

    for (double y : y_positions) {
        if (y < state.bbox[1] || y > state.bbox[3]) continue;

        bool skip = false;
        for (double p : state.v_exclude) {
            double dist;
            if (scale == "log")
                dist = std::abs(std::log10(y) - std::log10(p));
            else
                dist = std::abs(y - p);
            if (dist < state.position_tolerance) { skip = true; break; }
        }
        if (skip) continue;

        Point2d pt = diagram.transform(Point2d(state.x_axis_location, y));
        XmlNode line_el = mk_line(
            Point2d(pt[0] - state.v_tick_direction * state.ticksize[0], pt[1]),
            Point2d(pt[0] + state.v_tick_direction * state.ticksize[1], pt[1]),
            diagram, "", Eigen::VectorXd(), false);
        tick_grp.append_copy(line_el);
    }
}

// Find the live h_tick_group inside axes_group (the last 'g' child before labels)
static XmlNode find_h_tick_group(AxesState& state) {
    // We track whether appended; the group was added via append_copy
    // We need to find it in the axes_group. It's a 'g' element.
    // We'll search for it by tag. In practice, the h_tick_group is a <g> we added.
    // Since we may have multiple <g> children, we use the state.h_tick_group node
    // which was the scratch copy. But after append_copy the actual node is in axes_group.
    // We'll iterate and find the matching one by id.
    std::string id = state.h_tick_group.attribute("id").as_string("");
    for (auto child = state.axes_group.first_child(); child; child = child.next_sibling()) {
        if (std::string(child.name()) == "g" &&
            std::string(child.attribute("id").as_string("")) == id) {
            return child;
        }
    }
    return XmlNode(); // null
}

static XmlNode find_v_tick_group(AxesState& state) {
    std::string id = state.v_tick_group.attribute("id").as_string("");
    for (auto child = state.axes_group.first_child(); child; child = child.next_sibling()) {
        if (std::string(child.name()) == "g" &&
            std::string(child.attribute("id").as_string("")) == id) {
            return child;
        }
    }
    return XmlNode();
}

static void h_labels(AxesState& state, XmlNode element, Diagram& diagram, XmlNode parent) {
    std::string hlabels_str = element.attribute("hlabels").as_string("");
    if (state.decorations == "no" && hlabels_str.empty()) return;

    std::vector<double> h_exclude = state.h_exclude;
    auto scale = diagram.get_scales()[0];
    std::vector<double> h_positions;

    if (hlabels_str.empty()) {
        if (scale == "log") {
            h_positions = find_log_positions({state.bbox[0], state.bbox[2]});
        } else {
            auto labels = find_label_positions(
                {state.bbox[0], state.bbox[2]}, state.h_pi_format);
            int N = static_cast<int>(std::round((labels[2] - labels[0]) / labels[1]));
            for (int i = 0; i <= N; ++i) {
                h_positions.push_back(labels[0] + i * (labels[2] - labels[0]) / N);
            }
        }
        h_exclude.push_back(state.bbox[0]);
        h_exclude.push_back(state.bbox[2]);
    } else {
        try {
            Value val = diagram.expr_ctx().eval(hlabels_str);
            if (scale == "log") {
                auto& vec = val.as_vector();
                std::vector<double> r(vec.data(), vec.data() + vec.size());
                h_positions = find_log_positions(r);
            } else {
                auto& vec = val.as_vector();
                double start = vec[0], step = vec[1], end = vec[2];
                int N = static_cast<int>(std::round((end - start) / step));
                for (int i = 0; i <= N; ++i) {
                    h_positions.push_back(start + i * (end - start) / N);
                }
            }
        } catch (...) {
            spdlog::error("Error in <axes> parsing hlabels={}", hlabels_str);
            return;
        }
        if (state.h_pi_format) {
            for (auto& pos : h_positions) {
                pos /= M_PI;
            }
        }
    }

    double h_scale = 1.0;
    if (state.h_pi_format) h_scale = M_PI;

    // If h_tick_group hasn't been appended yet, do it now
    if (!state.h_tick_group_appended) {
        state.axes_group.append_copy(state.h_tick_group);
        state.h_tick_group_appended = true;
    }

    // Find the live h_tick_group in axes_group
    XmlNode tick_grp = find_h_tick_group(state);

    if (state.h_zero_label) {
        auto it = std::find_if(h_exclude.begin(), h_exclude.end(),
                               [](double v) { return std::abs(v) < 1e-10; });
        if (it != h_exclude.end()) h_exclude.erase(it);
    }

    bool commas = std::string(element.attribute("label-commas").as_string("yes")) == "yes";

    for (double x : h_positions) {
        if (x < state.bbox[0] || x > state.bbox[2]) continue;

        bool skip = false;
        for (double p : h_exclude) {
            double dist;
            if (scale == "log")
                dist = std::abs(std::log10(x * h_scale) - std::log10(p));
            else
                dist = std::abs(x * h_scale - p);
            if (dist < state.position_tolerance) { skip = true; break; }
        }
        if (skip) continue;

        // Create label
        auto scratch = diagram.get_scratch();
        auto xlabel = scratch.append_child("label");
        auto math_el = xlabel.append_child("m");

        if (scale == "log") {
            double x_text = std::log10(x);
            double frac = std::fmod(x_text, 1.0);
            if (frac < 0) frac += 1.0;
            int prefix_val = static_cast<int>(std::round(std::pow(10.0, frac)));
            std::string begin;
            double x_exp;
            if (prefix_val != 1) {
                x_exp = std::floor(x_text);
                begin = std::format("{}\\cdot10^{{", prefix_val);
            } else {
                x_exp = x_text;
                begin = "10^{";
            }
            math_el.text().set((begin + std::format("{:g}", x_exp) + "}").c_str());
            xlabel.append_attribute("scale").set_value("0.8");
        } else {
            math_el.text().set(label_text(x, commas, diagram).c_str());
        }

        if (state.h_pi_format) {
            math_el.text().set(get_pi_text(x).c_str());
        }

        xlabel.append_attribute("p").set_value(
            std::format("({},{})", x * h_scale, state.y_axis_location).c_str());

        if (state.tactile) {
            if (state.top_labels) {
                xlabel.append_attribute("alignment").set_value("hat");
                xlabel.append_attribute("offset").set_value("(0,0)");
            } else {
                xlabel.append_attribute("alignment").set_value("ha");
                xlabel.append_attribute("offset").set_value("(0,0)");
            }
        } else {
            if (state.top_labels) {
                xlabel.append_attribute("alignment").set_value("north");
                xlabel.append_attribute("offset").set_value("(0,7)");
            } else {
                xlabel.append_attribute("alignment").set_value("south");
                xlabel.append_attribute("offset").set_value("(0,-7)");
            }
        }

        xlabel.append_attribute("clear-background").set_value(
            state.clear_background.c_str());
        label_element(xlabel, diagram, parent, OutlineStatus::None);

        // Add tick mark
        Point2d pt = diagram.transform(Point2d(x * h_scale, state.y_axis_location));
        XmlNode line_el = mk_line(
            Point2d(pt[0], pt[1] + state.h_tick_direction * state.ticksize[0]),
            Point2d(pt[0], pt[1] - state.h_tick_direction * state.ticksize[1]),
            diagram, "", Eigen::VectorXd(), false);

        if (tick_grp) {
            tick_grp.append_copy(line_el);
        }
    }
}

static void v_labels(AxesState& state, XmlNode element, Diagram& diagram, XmlNode parent) {
    std::string vlabels_str = element.attribute("vlabels").as_string("");
    if (state.decorations == "no" && vlabels_str.empty()) return;

    std::vector<double> v_exclude = state.v_exclude;
    auto scale = diagram.get_scales()[1];
    std::vector<double> v_positions;

    if (vlabels_str.empty()) {
        if (scale == "log") {
            v_positions = find_log_positions({state.bbox[1], state.bbox[3]});
        } else {
            auto labels = find_label_positions(
                {state.bbox[1], state.bbox[3]}, state.v_pi_format);
            int N = static_cast<int>(std::round((labels[2] - labels[0]) / labels[1]));
            for (int i = 0; i <= N; ++i) {
                v_positions.push_back(labels[0] + i * (labels[2] - labels[0]) / N);
            }
        }
        v_exclude.push_back(state.bbox[1]);
        v_exclude.push_back(state.bbox[3]);
    } else {
        try {
            Value val = diagram.expr_ctx().eval(vlabels_str);
            if (scale == "log") {
                auto& vec = val.as_vector();
                std::vector<double> r(vec.data(), vec.data() + vec.size());
                v_positions = find_log_positions(r);
            } else {
                auto& vec = val.as_vector();
                double start = vec[0], step = vec[1], end = vec[2];
                int N = static_cast<int>(std::round((end - start) / step));
                for (int i = 0; i <= N; ++i) {
                    v_positions.push_back(start + i * (end - start) / N);
                }
            }
        } catch (...) {
            spdlog::error("Error in <axes> parsing vlabels={}", vlabels_str);
            return;
        }
        if (state.v_pi_format) {
            for (auto& pos : v_positions) {
                pos /= M_PI;
            }
        }
    }

    double v_scale = 1.0;
    if (state.v_pi_format) v_scale = M_PI;

    if (!state.v_tick_group_appended) {
        state.axes_group.append_copy(state.v_tick_group);
        state.v_tick_group_appended = true;
    }

    XmlNode tick_grp = find_v_tick_group(state);

    if (std::string(element.attribute("v-zero-label").as_string("no")) == "yes") {
        auto it = std::find_if(v_exclude.begin(), v_exclude.end(),
                               [](double v) { return std::abs(v) < 1e-10; });
        if (it != v_exclude.end()) v_exclude.erase(it);
    }

    bool commas = std::string(element.attribute("label-commas").as_string("yes")) == "yes";

    for (double y : v_positions) {
        if (y < state.bbox[1] || y > state.bbox[3]) continue;

        bool skip = false;
        for (double p : v_exclude) {
            double dist;
            if (scale == "log")
                dist = std::abs(std::log10(y * v_scale) - std::log10(p));
            else
                dist = std::abs(y * v_scale - p);
            if (dist < state.position_tolerance) { skip = true; break; }
        }
        if (skip) continue;

        auto scratch = diagram.get_scratch();
        auto ylabel = scratch.append_child("label");
        auto math_el = ylabel.append_child("m");

        if (scale == "log") {
            double y_text = std::log10(y);
            double frac = std::fmod(y_text, 1.0);
            if (frac < 0) frac += 1.0;
            int prefix_val = static_cast<int>(std::round(std::pow(10.0, frac)));
            std::string begin;
            double y_exp;
            if (prefix_val != 1) {
                y_exp = std::floor(y_text);
                begin = std::format("{}\\cdot10^{{", prefix_val);
            } else {
                y_exp = y_text;
                begin = "10^{";
            }
            math_el.text().set((begin + std::format("{:g}", y_exp) + "}").c_str());
            ylabel.append_attribute("scale").set_value("0.8");
        } else {
            math_el.text().set(label_text(y, commas, diagram).c_str());
        }

        if (state.v_pi_format) {
            math_el.text().set(get_pi_text(y).c_str());
        }

        ylabel.append_attribute("p").set_value(
            std::format("({},{})", state.x_axis_location, y * v_scale).c_str());

        if (state.tactile) {
            if (state.right_labels) {
                ylabel.append_attribute("alignment").set_value("east");
                ylabel.append_attribute("offset").set_value("(25, 0)");
            } else {
                ylabel.append_attribute("alignment").set_value("va");
                ylabel.append_attribute("offset").set_value("(-25, 0)");
            }
        } else {
            if (state.right_labels) {
                ylabel.append_attribute("alignment").set_value("east");
                ylabel.append_attribute("offset").set_value("(7,0)");
            } else {
                ylabel.append_attribute("alignment").set_value("west");
                ylabel.append_attribute("offset").set_value("(-7,0)");
            }
        }

        ylabel.append_attribute("clear-background").set_value(
            state.clear_background.c_str());
        label_element(ylabel, diagram, parent, OutlineStatus::None);

        Point2d pt = diagram.transform(Point2d(state.x_axis_location, y * v_scale));
        XmlNode line_el = mk_line(
            Point2d(pt[0] - state.v_tick_direction * state.ticksize[0], pt[1]),
            Point2d(pt[0] + state.v_tick_direction * state.ticksize[1], pt[1]),
            diagram, "", Eigen::VectorXd(), false);

        if (tick_grp) {
            tick_grp.append_copy(line_el);
        }
    }
}

// ---------------------------------------------------------------------------
// axes() entry point
// ---------------------------------------------------------------------------

void axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) return;

    AxesState state;
    state.tactile = diagram.output_format() == OutputFormat::Tactile;
    state.stroke = element.attribute("stroke").as_string("black");
    state.thickness = element.attribute("thickness").as_string("2");

    // Create axes group
    std::string default_id = diagram.prepend_id_prefix("axes");
    state.axes_group = parent.append_child("g");
    state.axes_group.append_attribute("id").set_value(
        element.attribute("id").as_string(default_id.c_str()));
    state.axes_group.append_attribute("stroke").set_value(state.stroke.c_str());
    state.axes_group.append_attribute("stroke-width").set_value(state.thickness.c_str());

    cliptobbox(state.axes_group, element, diagram);
    diagram.register_svg_element(element, state.axes_group, false);

    // Parse axes attribute
    std::string axes_attr = element.attribute("axes").as_string("");
    if (axes_attr == "all") {
        axes_attr = "";
        element.remove_attribute("axes");
    }
    state.axes_attribute = axes_attr;
    state.axes_attribute_set = !axes_attr.empty();

    std::string h_check = element.attribute("axes").as_string("horizontal");
    state.horizontal_axis = (h_check == "horizontal");
    std::string v_check = element.attribute("axes").as_string("vertical");
    state.vertical_axis = (v_check == "vertical");

    state.clear_background = element.attribute("clear-background").as_string("no");
    state.decorations = element.attribute("decorations").as_string("yes");
    state.h_pi_format = std::string(element.attribute("h-pi-format").as_string("no")) == "yes";
    state.v_pi_format = std::string(element.attribute("v-pi-format").as_string("no")) == "yes";

    // Tick size
    if (state.tactile) {
        state.ticksize = {18.0, 0.0};
    } else {
        state.ticksize = {3.0, 3.0};
        std::string ts_str = element.attribute("tick-size").as_string("");
        if (!ts_str.empty()) {
            try {
                Value ts_val = diagram.expr_ctx().eval(ts_str);
                if (ts_val.is_double()) {
                    double v = ts_val.as_double();
                    state.ticksize = {v, v};
                } else if (ts_val.is_vector()) {
                    auto& vec = ts_val.as_vector();
                    state.ticksize = {vec[0], vec[1]};
                }
            } catch (...) {
                spdlog::error("Error in <axes> parsing tick-size={}", ts_str);
            }
        }
    }

    state.bbox = diagram.bbox();
    state.position_tolerance = 1e-10;

    // Arrows
    try {
        state.arrows = std::stoi(element.attribute("arrows").as_string("0"));
    } catch (...) {
        spdlog::error("Error in <axes> parsing arrows={}",
                       element.attribute("arrows").as_string(""));
        state.arrows = 0;
    }

    position_axes(state, element, diagram);
    apply_axis_labels(state, element, diagram, parent);

    // Bounding box rectangle
    if (std::string(element.attribute("bounding-box").as_string("no")) == "yes") {
        auto rect = state.axes_group.append_child("rect");
        Point2d ul = diagram.transform(Point2d(state.bbox[0], state.bbox[3]));
        Point2d lr = diagram.transform(Point2d(state.bbox[2], state.bbox[1]));
        double w = lr[0] - ul[0];
        double h = lr[1] - ul[1];
        rect.append_attribute("x").set_value(float2str(ul[0]).c_str());
        rect.append_attribute("y").set_value(float2str(ul[1]).c_str());
        rect.append_attribute("width").set_value(float2str(w).c_str());
        rect.append_attribute("height").set_value(float2str(h).c_str());
        rect.append_attribute("fill").set_value("none");
    }

    if (state.horizontal_axis) {
        add_h_axis(state, element, diagram);
        // Create h_tick_group in scratch
        state.h_tick_group = diagram.get_scratch().append_child("g");
        diagram.add_id(state.h_tick_group);
        horizontal_ticks(state, element, diagram);
        h_labels(state, element, diagram, parent);
    }
    if (state.vertical_axis) {
        add_v_axis(state, element, diagram);
        state.v_tick_group = diagram.get_scratch().append_child("g");
        diagram.add_id(state.v_tick_group);
        vertical_ticks(state, element, diagram);
        v_labels(state, element, diagram, parent);
    }

    // Set global state for tick_mark
    static AxesState s_saved_state;
    s_saved_state = state;
    g_axes_state = &s_saved_state;
}

// ---------------------------------------------------------------------------
// tick_mark
// ---------------------------------------------------------------------------

void tick_mark(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) return;

    std::string axis = element.attribute("axis").as_string("horizontal");
    bool tactile = diagram.output_format() == OutputFormat::Tactile;

    double location_val = 0;
    bool location_is_point = false;
    Point2d location_pt;

    std::string loc_str = element.attribute("location").as_string("0");
    try {
        Value val = diagram.expr_ctx().eval(loc_str);
        if (val.is_vector() && val.as_vector().size() >= 2) {
            location_pt = val.as_point();
            location_is_point = true;
        } else {
            location_val = val.to_double();
        }
    } catch (...) {
        spdlog::error("Error in <tick-mark> parsing location={}", loc_str);
        return;
    }

    double y_axis_location = 0;
    double x_axis_location = 0;
    bool top_labels = false;
    bool right_labels = false;
    if (g_axes_state) {
        y_axis_location = g_axes_state->y_axis_location;
        x_axis_location = g_axes_state->x_axis_location;
        top_labels = g_axes_state->top_labels;
        right_labels = g_axes_state->right_labels;
    }

    Point2d location;
    if (location_is_point) {
        location = location_pt;
    } else {
        if (axis == "horizontal")
            location = Point2d(location_val, y_axis_location);
        else
            location = Point2d(x_axis_location, location_val);
    }
    Point2d p = diagram.transform(location);

    // Tick size
    std::array<double, 2> size = {3.0, 3.0};
    if (g_axes_state) size = g_axes_state->ticksize;

    std::string size_str = element.attribute("size").as_string("");
    if (!size_str.empty()) {
        try {
            Value val = diagram.expr_ctx().eval(size_str);
            if (val.is_double()) {
                double v = val.as_double();
                size = {v, v};
            } else if (val.is_vector()) {
                auto& vec = val.as_vector();
                size = {vec[0], vec[1]};
            }
        } catch (...) {
            // use default
        }
    } else if (!g_axes_state) {
        size = {3.0, 3.0};
    }

    if (tactile) size = {18.0, 0.0};

    int tick_direction = 1;
    XmlNode line_el;
    if (axis == "horizontal") {
        if (g_axes_state) tick_direction = g_axes_state->h_tick_direction;
        line_el = mk_line(
            Point2d(p[0], p[1] + tick_direction * size[0]),
            Point2d(p[0], p[1] - tick_direction * size[1]),
            diagram, "", Eigen::VectorXd(), false);
    } else {
        if (g_axes_state) tick_direction = g_axes_state->v_tick_direction;
        line_el = mk_line(
            Point2d(p[0] - tick_direction * size[0], p[1]),
            Point2d(p[0] + tick_direction * size[1], p[1]),
            diagram, "", Eigen::VectorXd(), false);
    }

    diagram.register_svg_element(element, line_el);

    std::string thickness = element.attribute("thickness").as_string("");
    if (thickness.empty()) {
        thickness = g_axes_state ? g_axes_state->thickness : "2";
    }
    std::string stroke = element.attribute("stroke").as_string("");
    if (stroke.empty()) {
        stroke = g_axes_state ? g_axes_state->stroke : "black";
    }
    if (tactile) {
        thickness = "2";
        stroke = "black";
    }

    line_el.append_attribute("stroke-width").set_value(thickness.c_str());
    line_el.append_attribute("stroke").set_value(stroke.c_str());
    parent.append_copy(line_el);

    // Check for label content
    std::string el_text;
    const char* raw_text = element.text().get();
    if (raw_text) {
        el_text = raw_text;
        // trim
        auto start = el_text.find_first_not_of(" \t\n\r");
        if (start == std::string::npos) el_text.clear();
        else el_text = el_text.substr(start);
    }

    bool has_children = false;
    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_element) { has_children = true; break; }
    }

    if ((!el_text.empty()) || has_children) {
        // Create a copy of element for label processing
        auto scratch = diagram.get_scratch();
        auto el_copy = scratch.append_copy(element);

        std::string align, off;
        if (axis == "horizontal") {
            if (tactile) {
                align = top_labels ? "hat" : "ha";
                off = "(0,0)";
            } else {
                if (top_labels) { align = "north"; off = "(0,7)"; }
                else            { align = "south"; off = "(0,-7)"; }
            }
        } else {
            if (tactile) {
                if (right_labels) { align = "east"; off = "(25,0)"; }
                else              { align = "va";   off = "(-25,0)"; }
            } else {
                if (right_labels) { align = "east"; off = "(7,0)"; }
                else              { align = "west"; off = "(-7,0)"; }
            }
        }

        if (!el_copy.attribute("alignment")) {
            el_copy.append_attribute("alignment").set_value(align.c_str());
            if (!el_copy.attribute("offset")) {
                el_copy.append_attribute("offset").set_value(off.c_str());
            }
        }
        if (!el_copy.attribute("user-coords"))
            el_copy.append_attribute("user-coords").set_value("no");
        else
            el_copy.attribute("user-coords").set_value("no");

        if (!el_copy.attribute("anchor"))
            el_copy.append_attribute("anchor").set_value(pt2str(p, ",").c_str());
        else
            el_copy.attribute("anchor").set_value(pt2str(p, ",").c_str());

        label_element(el_copy, diagram, parent, status);
    }
}

}  // namespace prefigure
