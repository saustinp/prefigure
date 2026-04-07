#pragma once

#include "types.hpp"

#include <Eigen/Dense>
#include <pugixml.hpp>

#include <string>
#include <unordered_map>

namespace prefigure {

/**
 * @brief Resolve a color name to its hex representation.
 *
 * Looks up the name in an internal dictionary of known color aliases
 * (e.g., "gray" -> "#777", "lightgray" -> "#ccc", "darkgray" -> "#333").
 * If the name is not found, it is returned unchanged (assumed to be a
 * valid CSS color or hex string).
 *
 * @param color A color name, hex string, or "none".
 * @return The resolved color string, or "none" if the input is empty or "none".
 */
std::string get_color(const std::string& color);

/**
 * @brief Bulk-set XML attributes on an element from a key-value map.
 *
 * @param element The XML node to modify.
 * @param attrs   Map of attribute name -> value pairs to add.
 */
void add_attr(XmlNode element, const std::unordered_map<std::string, std::string>& attrs);

/**
 * @brief Read an XML attribute, returning a default if absent.
 *
 * @param element     The XML node to query.
 * @param attr        The attribute name.
 * @param default_val Value to return if the attribute does not exist.
 * @return The attribute's string value, or @p default_val.
 */
std::string get_attr(XmlNode element, const std::string& attr, const std::string& default_val);

/**
 * @brief Read an XML attribute and evaluate it through the expression context.
 *
 * Mirrors Python's `util.get_attr()` semantics:
 *  1. Read the raw attribute value (or default)
 *  2. Try to evaluate it through @p ctx
 *  3. If evaluation succeeds, stringify the result (vectors are joined with commas)
 *  4. If evaluation fails (unknown name, syntax error), return the raw string
 *
 * Used for attributes that may reference user-defined names — for example
 * `alignment="alignments[k]"` inside a `<repeat>` loop.
 *
 * @param element     The XML node to query.
 * @param ctx         The expression context to evaluate against.
 * @param attr        The attribute name.
 * @param default_val Value to return if the attribute does not exist.
 * @return The (possibly evaluated) string form of the attribute.
 */
std::string get_attr(XmlNode element, ExpressionContext& ctx,
                     const std::string& attr, const std::string& default_val);

/**
 * @brief Ensure an XML attribute exists, setting it to a default if absent.
 *
 * If the attribute already exists, its value is preserved (and could be
 * subject to future ${...} expression substitution).
 *
 * @param element     The XML node to modify.
 * @param attr        The attribute name.
 * @param default_val Fallback value if the attribute does not exist.
 */
void set_attr(XmlNode element, const std::string& attr, const std::string& default_val);

/**
 * @brief Extract 1D SVG styling attributes from an XML element.
 *
 * Reads: `stroke`, `stroke-opacity`, `opacity`, `thickness` (mapped to
 * `stroke-width`), `miterlimit`, `linejoin`, `linecap`, `dash` (mapped
 * to `stroke-dasharray`), and `fill` (defaulting to "none").
 *
 * This is intended for elements that are stroked but not filled (lines,
 * curves, etc.).
 *
 * @param element The source XML element.
 * @return A map of SVG attribute name -> value strings.
 *
 * @see get_2d_attr() for elements that also have a fill.
 */
std::unordered_map<std::string, std::string> get_1d_attr(XmlNode element);

/**
 * @brief Extract 2D SVG styling attributes from an XML element.
 *
 * Calls get_1d_attr() and then overrides the fill with the resolved color,
 * and adds `fill-rule` and `fill-opacity` if present.
 *
 * This is intended for elements that have both stroke and fill (rectangles,
 * polygons, circles, etc.).
 *
 * @param element The source XML element.
 * @return A map of SVG attribute name -> value strings.
 *
 * @see get_1d_attr()
 */
std::unordered_map<std::string, std::string> get_2d_attr(XmlNode element);

/**
 * @brief Apply a bounding-box clip path if the element requests it.
 *
 * Checks the `cliptobbox` attribute on @p element.  If set to anything
 * other than "no", applies the diagram's current clip path to @p g_element.
 *
 * @param g_element The SVG `<g>` element to receive the clip-path attribute.
 * @param element   The source XML element with the `cliptobbox` attribute.
 * @param diagram   The Diagram providing the current clip-path ID.
 */
void cliptobbox(XmlNode g_element, XmlNode element, Diagram& diagram);

/**
 * @brief Format a double with 1 decimal place (e.g., "3.1").
 * @param x The value to format.
 * @return The formatted string.
 */
std::string float2str(double x);

/**
 * @brief Format a double with 4 decimal places (e.g., "3.1416").
 * @param x The value to format.
 * @return The formatted string.
 */
std::string float2longstr(double x);

/**
 * @brief Format a 2D point as a string with 1 decimal place.
 *
 * @param p      The point to format.
 * @param spacer Separator between x and y (default: " ").
 * @param paren  If true, wrap the output in parentheses.
 * @return A string like "1.0 2.0" or "(1.0,2.0)".
 */
std::string pt2str(const Point2d& p, const std::string& spacer = " ", bool paren = false);

/**
 * @brief Format an N-dimensional vector as a string with 1 decimal place.
 *
 * @param p      The vector to format.
 * @param spacer Separator between components (default: " ").
 * @param paren  If true, wrap the output in parentheses.
 * @return A string like "1.0 2.0 3.0".
 */
std::string pt2str(const Eigen::VectorXd& p, const std::string& spacer = " ", bool paren = false);

/**
 * @brief Format a 2D point as a string with 4 decimal places.
 *
 * @param p      The point to format.
 * @param spacer Separator between x and y (default: " ").
 * @return A string like "1.0000 2.0000".
 */
std::string pt2long_str(const Point2d& p, const std::string& spacer = " ");

/**
 * @brief Format a 2D point in numpy-style parenthesized comma notation: "(x,y)".
 *
 * Uses 1 decimal place formatting.
 *
 * @param p The point to format.
 * @return A string like "(1.0,2.0)".
 */
std::string np2str(const Point2d& p);

}  // namespace prefigure
