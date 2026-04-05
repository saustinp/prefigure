#pragma once

#include "types.hpp"

#include <optional>
#include <string>

namespace prefigure {

// Add a tactile arrowhead marker to the diagram's reusables.
// Returns the marker ID.
std::string add_tactile_arrowhead_marker(Diagram& diagram, XmlNode path, bool mid = false);

// Add an arrowhead marker (non-tactile or delegates to tactile).
// Returns the marker ID, or empty string on error.
std::string add_arrowhead_marker(Diagram& diagram, XmlNode path, bool mid = false,
                                 const std::string& arrow_width_str = "",
                                 const std::string& arrow_angles_str = "");

// Add an arrowhead to a path at the given location (e.g. "marker-end").
// Returns the marker ID.
std::string add_arrowhead_to_path(Diagram& diagram, const std::string& location,
                                  XmlNode path,
                                  const std::string& arrow_width_str = "",
                                  const std::string& arrow_angles_str = "");

// Get the arrow length for a given marker ID.
double get_arrow_length(const std::string& key);

}  // namespace prefigure
