#pragma once

#include "types.hpp"

#ifdef PREFIGURE_HAS_NETWORK

namespace prefigure {

/**
 * @brief Render a `<network>` XML element as SVG.
 *
 * Draws a graph/network diagram with nodes and edges.  Node positions
 * can be specified explicitly or computed via a layout algorithm.
 * Edges are drawn between named nodes.
 *
 * @par XML Attributes
 * - `name` (required): Identifier for the network (used to store/retrieve coordinates).
 * - `vertices` (required): List of vertex names.
 * - `edges` (required): List of edge pairs.
 * - `layout` (optional): Layout algorithm name.
 * - `stroke`, `fill`, etc.: Standard styling attributes for edges and nodes.
 *
 * @par SVG Output
 * Creates `<line>` elements for edges and `<circle>` elements for nodes,
 * grouped in a `<g>` element.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see Diagram::save_network_coordinates(), Diagram::get_network_coordinates()
 */
void network(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure

#endif  // PREFIGURE_HAS_NETWORK
