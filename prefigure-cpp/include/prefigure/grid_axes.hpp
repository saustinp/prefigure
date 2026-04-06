#pragma once

#include "types.hpp"

#include <array>
#include <string>
#include <vector>

namespace prefigure {

/**
 * @brief Lookup table for automatic grid line spacing.
 *
 * Maps `round(2 * distance)` to a delta multiplier, where `distance` is
 * the normalized coordinate range (scaled to lie in (1, 10]).  Uses finer
 * defaults than label_delta_map() to produce denser grid lines:
 * - Ranges ~1–1.5 → delta 0.1
 * - Ranges ~1.5–2.5 → delta 0.25
 * - Ranges ~2.5–5.5 → delta 0.5
 * - Ranges ~6–10 → delta 1.0
 *
 * @return Reference to the static lookup map.
 */
inline const std::unordered_map<int, double>& grid_delta_map() {
    static const std::unordered_map<int, double> m = {
        {2, 0.1}, {3, 0.25}, {4, 0.25}, {5, 0.5},
        {6, 0.5}, {7, 0.5}, {8, 0.5}, {9, 0.5}, {10, 0.5},
        {11, 0.5}, {12, 1}, {13, 1}, {14, 1}, {15, 1}, {16, 1},
        {17, 1}, {18, 1}, {19, 1}, {20, 1}
    };
    return m;
}

/**
 * @brief Find automatic grid spacing for a coordinate range.
 *
 * Normalizes the range to (1, 10], looks up a "nice" delta via
 * grid_delta_map(), then scales back.  Returns start/step/end
 * values aligned to the delta so grid lines land on round numbers.
 *
 * @param coordinate_range The [min, max] range of the coordinate axis.
 * @param pi_format If true, choose spacing as multiples of pi.
 * @return Array {x0, dx, x1} for iteration: x0, x0+dx, ..., x1.
 */
std::array<double, 3> find_gridspacing(
    const std::array<double, 2>& coordinate_range,
    bool pi_format = false);

/**
 * @brief Find log-scale grid positions.
 *
 * Similar to find_log_positions() in axes.hpp but uses different
 * default spacing suitable for grid lines rather than axis labels.
 *
 * @param r A vector of 2 or 3 elements: [start, end] or
 *          [start, spacing_hint, end].
 * @return Vector of grid line positions.
 */
std::vector<double> find_grid_log_positions(const std::vector<double>& r);

/**
 * @brief Generate linearly spaced positions from a start/step/end triple.
 *
 * @param r Array {x0, dx, x1}.
 * @return Vector of positions: x0, x0+dx, x0+2*dx, ..., up to x1.
 */
std::vector<double> find_linear_positions(const std::array<double, 3>& r);

/**
 * @brief Render a `<grid>` XML element as SVG.
 *
 * Draws rectangular or polar grid lines.  For rectangular grids, draws
 * horizontal and vertical lines at automatically computed spacings.
 * For polar grids, draws concentric circles and radial spokes.
 * Supports custom basis vectors for non-orthogonal grids.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void grid(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<grid-axes>` XML element as SVG.
 *
 * Convenience element that renders both a grid and axes in a single
 * element.  Equivalent to a `<grid>` followed by an `<axes>` with
 * matching parameters.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void grid_axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Check whether an XML tag name is an axes-related sub-element.
 *
 * Returns true for tags that should be processed within an axes context
 * (e.g. tick-mark).
 *
 * @param tag The tag name to test.
 * @return True if the tag is an axes sub-element.
 */
bool is_axes_tag(const std::string& tag);

}  // namespace prefigure
