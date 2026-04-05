#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<definition>` XML element by registering a mathematical definition.
 *
 * Reads the text content of the element and passes it to
 * ExpressionContext::define(), which handles both variable assignments
 * (e.g., "a = 3") and function definitions (e.g., "f(x) = x^2").
 *
 * @par XML Attributes
 * - Text content (required): The definition string, e.g., "f(x) = x^2 + 1".
 * - `substitution` (optional, default: "yes"): Set to "no" to disable `^` preprocessing.
 * - `id-suffix` (optional): If present, pushes an ID suffix scope and parses children.
 *
 * @par SVG Output
 * None -- this element only modifies the expression namespace.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node (unused).
 * @param status  Outline rendering pass (unused).
 *
 * @see ExpressionContext::define()
 */
void definition(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<derivative>` XML element by registering a numerical derivative function.
 *
 * Creates a new function in the expression namespace that computes the
 * numerical derivative of an existing function using Richardson extrapolation.
 *
 * @par XML Attributes
 * - `function` (required): Name of the function to differentiate.
 * - `name` (required): Name for the derivative function.
 *
 * @par SVG Output
 * None -- this element only modifies the expression namespace.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node (unused).
 * @param status  Outline rendering pass (unused).
 *
 * @see ExpressionContext::register_derivative()
 */
void definition_derivative(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
