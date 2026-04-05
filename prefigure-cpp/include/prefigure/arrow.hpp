#pragma once

#include "types.hpp"

#include <optional>
#include <string>

namespace prefigure {

/**
 * @brief Create and register a tactile arrowhead marker in the diagram's `<defs>`.
 *
 * Constructs an arrowhead shaped for embossed/tactile output, scaled to the
 * path's stroke width.  The marker is registered as a reusable element so
 * that duplicate markers with the same stroke width are not recreated.
 *
 * @param diagram The diagram to register the marker in.
 * @param path    The SVG path element whose `stroke-width` determines the arrow size.
 * @param mid     If true, create a mid-line marker variant (default: false).
 * @return The marker element ID string.
 *
 * @see add_arrowhead_marker()
 */
std::string add_tactile_arrowhead_marker(Diagram& diagram, XmlNode path, bool mid = false);

/**
 * @brief Create and register an arrowhead marker in the diagram's `<defs>`.
 *
 * For tactile output, delegates to add_tactile_arrowhead_marker().
 * For SVG output, creates a standard arrowhead with configurable width
 * and tip angles.
 *
 * @param diagram          The diagram to register the marker in.
 * @param path             The SVG path element providing stroke context.
 * @param mid              If true, create a mid-line marker variant (default: false).
 * @param arrow_width_str  Optional custom arrow width (evaluated as expression).
 * @param arrow_angles_str Optional custom tip half-angle in degrees.
 * @return The marker element ID string, or empty on error.
 *
 * @see add_arrowhead_to_path()
 */
std::string add_arrowhead_marker(Diagram& diagram, XmlNode path, bool mid = false,
                                 const std::string& arrow_width_str = "",
                                 const std::string& arrow_angles_str = "");

/**
 * @brief Attach an arrowhead marker to a path at the specified location.
 *
 * Creates the marker via add_arrowhead_marker() and sets the appropriate
 * `marker-end`, `marker-start`, or `marker-mid` attribute on the path.
 *
 * @param diagram          The diagram context.
 * @param location         SVG marker attribute name: "marker-end", "marker-start", or "marker-mid".
 * @param path             The SVG path element to attach the arrowhead to.
 * @param arrow_width_str  Optional custom arrow width.
 * @param arrow_angles_str Optional custom tip half-angle.
 * @return The marker element ID string.
 */
std::string add_arrowhead_to_path(Diagram& diagram, const std::string& location,
                                  XmlNode path,
                                  const std::string& arrow_width_str = "",
                                  const std::string& arrow_angles_str = "");

/**
 * @brief Look up the arrow length for a previously created arrowhead marker.
 *
 * Arrow length is the distance from the tip to the base of the arrowhead,
 * used for adjusting endpoint positions so the line ends at the arrow tip.
 *
 * @param key The marker ID string.
 * @return The arrow length in SVG pixels, or 0.0 if the key is unknown.
 */
double get_arrow_length(const std::string& key);

}  // namespace prefigure
