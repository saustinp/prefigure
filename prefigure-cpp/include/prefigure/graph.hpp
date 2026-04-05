#pragma once

#include "types.hpp"

#include <functional>
#include <string>
#include <vector>

namespace prefigure {

/**
 * @brief Render a `<graph>` XML element as SVG.
 *
 * Plots the graph of a single-variable function y = f(x) (Cartesian) or
 * r = f(theta) (polar).  Samples the function over a domain, handles
 * discontinuities by breaking the path, and supports arrowheads.
 *
 * @par XML Attributes
 * - `function` (required): The function expression (must be defined in the namespace).
 * - `domain` (optional): Evaluation interval "(a, b)"; defaults to bbox x-range
 *   (Cartesian) or [0, 2*pi] (polar).
 * - `coordinates` (optional, default: "cartesian"): "cartesian" or "polar".
 * - `N` (optional, default: "200"): Number of sample points.
 * - `arrows` (optional, default: "0"): Number of arrowheads.
 * - `stroke`, `thickness`, `dash`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates one or more `<path>` elements (split at discontinuities).
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see Diagram::expr_ctx() for function lookup.
 */
void graph(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
