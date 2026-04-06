#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<tangent-line>` XML element as SVG.
 *
 * Computes the tangent line to a function at a given point using numerical
 * differentiation (Richardson extrapolation via calculus::derivative) and
 * renders it as a line or path element.
 *
 * For linear/linear scales, draws a straight `<line>` between the domain
 * endpoints.  For logarithmic scales, samples the tangent function at 101
 * points to produce a smooth `<path>`, skipping negative y-values on log-y.
 *
 * @par Function Registration
 * If the `@name` attribute is present, the tangent line function
 * T(x) = y0 + m*(x - a) is registered in the expression namespace
 * under that name, making it available to subsequent elements.
 *
 * @par XML Attributes
 * - `function` (required): Name of the function to differentiate.
 * - `point` (required): The x-coordinate at which to compute the tangent.
 * - `domain` (optional): Evaluation interval "(a, b)"; defaults to bbox x-range.
 * - `infinite` (optional): "yes" to extend line to diagram edges.
 * - `name` (optional): Register the tangent function under this name.
 * - `stroke` (optional, default "red"): Stroke color.
 * - `thickness` (optional, default "2"): Stroke width.
 * - `cliptobbox` (optional, default "yes"): Clip to bounding box.
 * - `outline` (optional): "yes" for outline rendering.
 *
 * @par SVG Output
 * Creates a `<line>` element (linear scales) or a `<path>` element
 * (logarithmic scales).
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see calculus.hpp for derivative()
 */
void tangent(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
