#pragma once

#include "types.hpp"

#ifdef PREFIGURE_HAS_SHAPES

#include <string>
#include <vector>

namespace prefigure {

/**
 * @brief Render a `<define-shapes>` XML element by registering named shapes.
 *
 * Iterates over child elements, renders each to SVG via tags::parse_element,
 * strips stroke/fill, and stores the resulting `<path>` in the diagram's
 * shape dictionary for later use by `<shape>` elements.
 *
 * @par Allowed child tags
 * arc, area-between-curves, area-under-curve, circle, ellipse, graph,
 * parametric-curve, path, polygon, rectangle, shape, spline
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node (unused for definition-only output).
 * @param status  Outline rendering pass.
 *
 * @see Diagram::add_shape()
 */
void shape_define(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<shape>` XML element as SVG.
 *
 * Recalls previously defined shapes by ID, optionally applies a boolean
 * geometry operation (union, intersection, difference, symmetric-difference,
 * convex-hull), and creates the resulting SVG `<path>`.
 *
 * Boolean operations use the GEOS C API to perform computational geometry.
 * SVG path `d` attributes are parsed into GEOS polygons (with Bezier curves
 * discretized), the operation is applied, and the result is converted back
 * to an SVG path string.
 *
 * @par XML Attributes
 * - `shape` or `shapes` (required): Comma-separated list of shape IDs.
 * - `operation` (optional): "union", "intersection", "difference",
 *   "symmetric-difference"/"sym-diff", or "convex-hull".
 *   Defaults to "union" when multiple shapes are given.
 * - `stroke`, `fill`, `thickness`, etc.: Standard styling attributes.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see Diagram::recall_shape(), shape_define()
 */
void shape(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure

#endif  // PREFIGURE_HAS_SHAPES
