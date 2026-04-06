#pragma once

#include "types.hpp"

#include <array>
#include <string>
#include <vector>

namespace prefigure {

/**
 * @brief Lookup table for automatic axis label tick spacing.
 *
 * Maps `round(2 * distance)` to a delta multiplier, where `distance` is
 * the normalized coordinate range (scaled to lie in (1, 10]).  This
 * bucket-based approach selects "nice" tick intervals:
 * - Ranges ~1–1.5 → delta 0.2 (5 ticks per unit)
 * - Ranges ~1.5–2.5 → delta 0.5
 * - Ranges ~2.5–5.5 → delta 1.0
 * - Ranges ~6–10 → delta 2.0
 *
 * The multiplier is then scaled back by the same power of 10 used
 * during normalization.
 *
 * @return Reference to the static lookup map.
 */
inline const std::unordered_map<int, double>& label_delta_map() {
    static const std::unordered_map<int, double> m = {
        {2, 0.2}, {3, 0.5}, {4, 0.5}, {5, 1},
        {6, 1}, {7, 1}, {8, 1}, {9, 1}, {10, 1}, {11, 1},
        {12, 2}, {13, 2}, {14, 2}, {15, 2}, {16, 2}, {17, 2},
        {18, 2}, {19, 2}, {20, 2}
    };
    return m;
}

/**
 * @brief Find automatic label positions for an axis range.
 *
 * Determines a "nice" starting position, spacing, and ending position
 * for tick labels on a linear axis.  Uses label_delta_map() to choose
 * a round delta that produces a readable number of ticks.
 *
 * @param coordinate_range The [min, max] range of the axis.
 * @param pi_format If true, choose spacing as multiples of pi.
 * @return Array {x0, dx, x1} suitable for iteration: x0, x0+dx, ..., x1.
 */
std::array<double, 3> find_label_positions(
    const std::array<double, 2>& coordinate_range,
    bool pi_format = false);

/**
 * @brief Find log-scale label positions given a range specification.
 *
 * @param r A vector of 2 or 3 elements: [start, end] or
 *          [start, spacing_hint, end].  Values represent powers of 10.
 * @return Vector of positions (as powers of 10) for tick marks.
 */
std::vector<double> find_log_positions(const std::vector<double>& r);

/**
 * @brief Return a LaTeX string for x*pi with rational fraction detection.
 *
 * Detects common fractions (halves, thirds, quarters, sixths) and
 * formats them as LaTeX: e.g. 0.5 → "\\frac{\\pi}{2}", 1.0 → "\\pi",
 * -2.0/3 → "-\\frac{2\\pi}{3}".
 *
 * @param x The multiplier of pi.
 * @return A LaTeX-formatted string.
 */
std::string get_pi_text(double x);

/**
 * @brief Format a number as a LaTeX `\\text{...}` string.
 *
 * @param x The number to format.
 * @param commas If true, insert comma separators for thousands.
 * @param diagram Reference to diagram for expression evaluation.
 * @return A LaTeX-formatted string wrapped in `\\text{...}`.
 */
std::string label_text(double x, bool commas, Diagram& diagram);

/**
 * @brief Internal state for axes construction.
 *
 * Holds positioning, tick, and label information computed during
 * the axes() entry point.  Shared with tick_mark() via a global
 * pointer so that manually placed tick marks can access axes geometry.
 */
struct AxesState {
    // -- General settings --

    bool tactile = false;              ///< True if rendering in tactile mode.
    std::string stroke = "black";      ///< Axis line stroke color.
    std::string thickness = "2";       ///< Axis line thickness.
    std::string clear_background = "no"; ///< "yes" to draw white background behind axes.
    std::string decorations = "yes";   ///< "yes" to draw tick marks and labels.
    bool h_pi_format = false;          ///< True if horizontal labels use pi fractions.
    bool v_pi_format = false;          ///< True if vertical labels use pi fractions.
    std::array<double, 2> ticksize = {3.0, 3.0}; ///< Tick mark half-lengths [major, minor].
    BBox bbox = {0, 0, 0, 0};         ///< Current bounding box of the axes' coordinate system.
    double position_tolerance = 1e-10; ///< Tolerance for position comparisons.
    int arrows = 0;                    ///< Number of arrowheads on axes (0, 1, or 2).

    // -- Horizontal axis positioning --

    double y_axis_location = 0;        ///< Y-coordinate where the horizontal axis is drawn.
    std::array<double, 2> y_axis_offsets = {0, 0}; ///< Left/right extension of horizontal axis beyond bbox.
    bool h_zero_include = false;       ///< True if zero should be included in horizontal tick range.
    bool top_labels = false;           ///< True to place horizontal labels above the axis.
    std::vector<double> h_exclude;     ///< Horizontal positions to skip when drawing ticks.
    bool h_zero_label = false;         ///< True to draw a label at x = 0 on the horizontal axis.
    int h_tick_direction = 1;          ///< Direction of horizontal ticks: 1 = down, -1 = up.
    bool horizontal_axis = true;       ///< False to suppress horizontal axis entirely.

    // -- Vertical axis positioning --

    double x_axis_location = 0;        ///< X-coordinate where the vertical axis is drawn.
    std::array<double, 2> x_axis_offsets = {0, 0}; ///< Bottom/top extension of vertical axis beyond bbox.
    bool v_zero_include = false;       ///< True if zero should be included in vertical tick range.
    bool right_labels = false;         ///< True to place vertical labels to the right of the axis.
    std::vector<double> v_exclude;     ///< Vertical positions to skip when drawing ticks.
    bool v_zero_label = false;         ///< True to draw a label at y = 0 on the vertical axis.
    int v_tick_direction = 1;          ///< Direction of vertical ticks: 1 = left, -1 = right.
    bool vertical_axis = true;         ///< False to suppress vertical axis entirely.

    // -- Axes attribute filtering --

    std::string axes_attribute;        ///< If set, only draw the axis specified ("x" or "y").
    bool axes_attribute_set = false;   ///< True if @axes attribute was specified.

    // -- SVG nodes --

    XmlNode axes_group;                ///< The `<g>` node containing all axes SVG output.
    XmlNode h_tick_group;              ///< Group node for horizontal tick marks.
    XmlNode v_tick_group;              ///< Group node for vertical tick marks.
    bool h_tick_group_appended = false; ///< True if h_tick_group has been added to parent.
    bool v_tick_group_appended = false; ///< True if v_tick_group has been added to parent.
};

/**
 * @brief Access the global pointer to the current axes state.
 *
 * Returns nullptr when no axes element is being processed.
 * Used by tick_mark() to access geometry from the enclosing axes().
 *
 * @return Pointer to the current AxesState, or nullptr.
 */
AxesState* get_axes_state();

/**
 * @brief Render an `<axes>` XML element as SVG.
 *
 * Draws horizontal and/or vertical axes with automatic tick marks,
 * labels, and optional arrowheads.  Supports linear, logarithmic,
 * and pi-formatted scales.  Determines axis placement based on
 * whether zero is in the coordinate range (crossing axes) or not
 * (frame-style axes at the edges).
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void axes(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<tick-mark>` XML element as SVG.
 *
 * Draws a manually placed tick mark at a specified location on the
 * current axes.  Must appear as a child of an `<axes>` element.
 * Accesses the enclosing axes geometry via get_axes_state().
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void tick_mark(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
