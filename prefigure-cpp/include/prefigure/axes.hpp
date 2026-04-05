#pragma once

#include "types.hpp"

#include <array>
#include <string>
#include <vector>

namespace prefigure {

/// Lookup table for automatic label tick spacing.
/// Maps round(2*distance) -> delta multiplier.
inline const std::unordered_map<int, double>& label_delta_map() {
    static const std::unordered_map<int, double> m = {
        {2, 0.2}, {3, 0.5}, {4, 0.5}, {5, 1},
        {6, 1}, {7, 1}, {8, 1}, {9, 1}, {10, 1}, {11, 1},
        {12, 2}, {13, 2}, {14, 2}, {15, 2}, {16, 2}, {17, 2},
        {18, 2}, {19, 2}, {20, 2}
    };
    return m;
}

/// Find automatic label positions for an axis range.
/// Returns (x0, dx, x1) for np.linspace-style iteration.
std::array<double, 3> find_label_positions(
    const std::array<double, 2>& coordinate_range,
    bool pi_format = false);

/// Find log-scale label positions given a range specification.
/// r has 2 or 3 elements: [start, end] or [start, spacing_hint, end].
std::vector<double> find_log_positions(const std::vector<double>& r);

/// Return a LaTeX string for x*pi with rational fraction detection.
std::string get_pi_text(double x);

/// Format a number as a LaTeX \text{...} string with optional commas.
std::string label_text(double x, bool commas, Diagram& diagram);

/**
 * @brief Internal Axes state used during axes construction.
 *
 * Holds positioning, tick, and label information computed during
 * the axes() entry point. Also accessible by tick_mark().
 */
struct AxesState {
    bool tactile = false;
    std::string stroke = "black";
    std::string thickness = "2";
    std::string clear_background = "no";
    std::string decorations = "yes";
    bool h_pi_format = false;
    bool v_pi_format = false;
    std::array<double, 2> ticksize = {3.0, 3.0};
    BBox bbox = {0, 0, 0, 0};
    double position_tolerance = 1e-10;
    int arrows = 0;

    // Horizontal axis positioning
    double y_axis_location = 0;
    std::array<double, 2> y_axis_offsets = {0, 0};
    bool h_zero_include = false;
    bool top_labels = false;
    std::vector<double> h_exclude;
    bool h_zero_label = false;
    int h_tick_direction = 1;
    bool horizontal_axis = true;

    // Vertical axis positioning
    double x_axis_location = 0;
    std::array<double, 2> x_axis_offsets = {0, 0};
    bool v_zero_include = false;
    bool right_labels = false;
    std::vector<double> v_exclude;
    bool v_zero_label = false;
    int v_tick_direction = 1;
    bool vertical_axis = true;

    // Axes element attribute (nullptr means "all")
    std::string axes_attribute;
    bool axes_attribute_set = false;

    // SVG group node for the axes
    XmlNode axes_group;

    // Tick group nodes
    XmlNode h_tick_group;
    XmlNode v_tick_group;
    bool h_tick_group_appended = false;
    bool v_tick_group_appended = false;
};

/// Global pointer to current axes state, used by tick_mark.
AxesState* get_axes_state();

/**
 * @brief Render an `<axes>` XML element as SVG.
 */
void axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<tick-mark>` XML element as SVG.
 */
void tick_mark(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
