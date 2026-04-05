#pragma once

#include "types.hpp"

#ifdef PREFIGURE_HAS_DIFFEQS

namespace prefigure {

/**
 * @brief Render a `<de-solve>` XML element by solving an ODE initial value problem.
 *
 * Numerically integrates an ODE system dy/dt = f(t, y) and stores the
 * solution trajectory in the expression namespace for subsequent plotting.
 *
 * @par XML Attributes
 * - `function` (required): Name of the ODE right-hand side function f(t, y).
 * - `initial-condition` (required): Initial state expression.
 * - `domain` (required): Time interval "(t0, t1)".
 * - `N` (optional): Number of integration steps.
 * - `name` (optional): Variable name to store the solution under.
 *
 * @par SVG Output
 * None -- this element only computes and stores data.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node (unused).
 * @param status  Outline rendering pass (unused).
 *
 * @see ExpressionContext::find_breaks(), ExpressionContext::measure_de_jump()
 */
void de_solve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<plot-de-solution>` XML element as SVG.
 *
 * Plots a previously computed ODE solution as a curve in the diagram.
 *
 * @par XML Attributes
 * - `solution` (required): Name of the stored ODE solution.
 * - `axes` (optional): Which components to plot, e.g., "(0,1)" for (t, y0).
 * - `stroke`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` element tracing the ODE solution curve.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void plot_de_solution(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure

#endif  // PREFIGURE_HAS_DIFFEQS
