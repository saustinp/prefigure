#pragma once

#include "types.hpp"

#include <array>
#include <string>
#include <vector>

namespace prefigure {

/// Lookup table for automatic grid spacing.
/// Maps round(2*distance) -> delta multiplier.
inline const std::unordered_map<int, double>& grid_delta_map() {
    static const std::unordered_map<int, double> m = {
        {2, 0.1}, {3, 0.25}, {4, 0.25}, {5, 0.5},
        {6, 0.5}, {7, 0.5}, {8, 0.5}, {9, 0.5}, {10, 0.5},
        {11, 0.5}, {12, 1}, {13, 1}, {14, 1}, {15, 1}, {16, 1},
        {17, 1}, {18, 1}, {19, 1}, {20, 1}
    };
    return m;
}

/// Find automatic grid spacing for a coordinate range.
/// Returns [x0, dx, x1].
std::array<double, 3> find_gridspacing(
    const std::array<double, 2>& coordinate_range,
    bool pi_format = false);

/// Find log-scale grid positions (different default spacing from axes version).
std::vector<double> find_grid_log_positions(const std::vector<double>& r);

/// Find linearly spaced positions from [start, step, end].
std::vector<double> find_linear_positions(const std::array<double, 3>& r);

/**
 * @brief Render a `<grid>` XML element as SVG.
 */
void grid(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<grid-axes>` XML element as SVG.
 */
void grid_axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Check whether an XML tag name is an axes-related sub-element.
 */
bool is_axes_tag(const std::string& tag);

}  // namespace prefigure
