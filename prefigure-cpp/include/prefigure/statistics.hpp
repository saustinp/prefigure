#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<scatter>` XML element as SVG.
 *
 * Plots a collection of data points as individual markers.  Data can come
 * from a named data source (matrix or namespace vectors) or from an explicit
 * points attribute.  Internally converts the element to a `<repeat>` over
 * a `<point>` template and delegates to the tag dispatcher.
 *
 * @par Data Sources
 * - **Matrix data** (`@data` + `@x`/`@y` as column indices): Extracts columns
 *   from a stored matrix and zips them into point pairs.
 * - **Namespace vectors** (`@data` + `@x`/`@y` as variable names): Retrieves
 *   two named vectors and zips them.
 * - **Direct points** (`@points`): Evaluates as a flat vector of (x,y) pairs.
 *
 * @par XML Attributes
 * - `data` (optional): Name of a data source in the expression namespace.
 * - `x`, `y` (required with `data`): Column indices or variable names.
 * - `points` (optional): Direct point list expression, alternative to `data`.
 * - `filter` (optional): Filter expression (partially supported).
 * - `style`, `size`, `fill`, `stroke`: Point marker styling.
 * - `at` (optional): Handle prefix for point elements.
 * - `point-text` (optional): Annotation text for each point.
 *
 * @par SVG Output
 * Creates multiple marker elements (circles, boxes, etc.) at data positions,
 * via the `<point>` handler inside a `<repeat>`.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void scatter(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<histogram>` XML element as SVG.
 *
 * Computes a histogram from data by binning values into equally spaced
 * intervals and rendering each bin as a rectangle.  Internally computes
 * bin counts (replacing scipy.ndimage.histogram from Python), stores
 * the results in the expression namespace, then converts the element to
 * a `<repeat>` over a `<rectangle>` template.
 *
 * @par XML Attributes
 * - `data` (required): Data source expression (evaluates to a vector).
 * - `bins` (optional, default 20): Number of bins.
 * - `min` (optional, default 0): Minimum data value for binning.
 * - `max` (optional): Maximum data value; defaults to data maximum.
 * - `fill`, `stroke`, `thickness`: Rectangle styling.
 * - `at` (optional): Handle prefix for bin rectangles.
 * - `bin-text` (optional): Annotation text template for each bin.
 *
 * @par SVG Output
 * Creates multiple `<rect>` elements, one per bin, via the `<rectangle>`
 * handler inside a `<repeat>`.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void histogram(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
