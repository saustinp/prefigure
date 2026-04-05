#pragma once

#include "types.hpp"

#include <string>
#include <vector>

namespace prefigure {

/**
 * @brief Generate SVG path commands for a circle, ellipse, or arc.
 *
 * Samples N points along the curve and produces "M" and "L" SVG path
 * commands in SVG pixel coordinates.  Supports rotation of the
 * ellipse axes.
 *
 * @param diagram       The diagram context for coordinate transforms.
 * @param center        Center point in user coordinates.
 * @param axes_length   Semi-axis lengths (x_radius, y_radius) in user coordinates.
 * @param angular_range Start and end angles in degrees, e.g., (0, 360) for a full circle.
 * @param rotate        Rotation of the ellipse axes in degrees (default: 0).
 * @param N             Number of sample points along the arc (default: 100).
 * @return A vector of SVG path command strings (alternating "M"/"L" and coordinate strings).
 */
std::vector<std::string> make_circle_path(Diagram& diagram,
                                          const Point2d& center,
                                          const Point2d& axes_length,
                                          const Point2d& angular_range,
                                          double rotate = 0.0,
                                          int N = 100);

/**
 * @brief Render a `<circle>` XML element as SVG.
 *
 * Draws a circle with a given center and radius.
 *
 * @par XML Attributes
 * - `center` (required): Center point expression.
 * - `radius` (required): Radius expression.
 * - `stroke`, `fill`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` element tracing the circle.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see make_circle_path()
 */
void circle_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render an `<ellipse>` XML element as SVG.
 *
 * Draws an ellipse with independent x and y radii and optional rotation.
 *
 * @par XML Attributes
 * - `center` (required): Center point expression.
 * - `axes` (required): Semi-axis lengths "(rx, ry)".
 * - `rotate` (optional, default: "0"): Rotation angle in degrees.
 * - `stroke`, `fill`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` element tracing the ellipse.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see make_circle_path()
 */
void ellipse(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render an `<arc>` XML element as SVG.
 *
 * Draws a circular or elliptical arc over a specified angular range.
 *
 * @par XML Attributes
 * - `center` (required): Center point expression.
 * - `radius` or `axes` (required): Radius or semi-axis lengths.
 * - `range` (required): Angular range in degrees, e.g., "(0, 90)".
 * - `arrows` (optional): Arrowhead configuration.
 * - `stroke`, `thickness`, etc.: Standard styling attributes.
 *
 * @par SVG Output
 * Creates a `<path>` element tracing the arc.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see make_circle_path()
 */
void arc(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render an `<angle-marker>` XML element as SVG.
 *
 * Draws a small arc or right-angle marker to indicate an angle between
 * two rays emanating from a vertex.
 *
 * @par XML Attributes
 * - `vertex` (required): The vertex point expression.
 * - `ray1`, `ray2` (required): Direction rays or endpoint expressions.
 * - `radius` (optional): Size of the angle marker.
 * - `right-angle` (optional): If "yes", draw a square right-angle symbol.
 *
 * @par SVG Output
 * Creates a `<path>` element for the angle marker arc or square.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 */
void angle_marker(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
