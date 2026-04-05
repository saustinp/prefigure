#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render a `<read>` XML element by loading external data.
 *
 * Reads data from an external file (CSV, JSON, etc.) and registers it
 * in the expression context for use by subsequent elements.
 *
 * @par XML Attributes
 * - `source` (required): Path to the external data file, relative to
 *   the diagram's external data directory.
 * - `name` (optional): Variable name under which to store the data.
 *
 * @par SVG Output
 * None -- this element only modifies the expression namespace.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node (unused).
 * @param status  Outline rendering pass (unused).
 *
 * @note Currently a stub implementation.
 */
void read(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
