#pragma once

#include "types.hpp"

#ifdef PREFIGURE_HAS_NETWORK

#include <Eigen/Dense>

#include <map>
#include <string>
#include <vector>

namespace prefigure {

/**
 * @brief Render a `<network>` XML element as SVG.
 *
 * Draws a graph/network diagram with nodes and edges.  Node positions
 * can be specified explicitly or computed via a layout algorithm.
 *
 * @par Node specification
 * Nodes are defined via `<node>` child elements with:
 * - `at` (handle/identifier)
 * - `p` (explicit position, optional)
 * - `edges` (list of adjacent node handles)
 *
 * Alternatively, a `graph` attribute provides an adjacency dictionary.
 *
 * @par Layout algorithms
 * When not all positions are given explicitly, a layout algorithm is used:
 * - `spring` (default): Fruchterman-Reingold force-directed layout
 * - `circular`: equally spaced on a circle
 * - `random`: random positions
 * - `bfs`: breadth-first search tree layout
 * - `spectral`: Laplacian eigenvector layout
 * - `planar`: Chrobak-Payne straight line drawing (requires Boost.Graph)
 * - `bipartite`: two-column layout
 *
 * @par Edge rendering
 * - Single edges: straight `<line>` elements
 * - Multi-edges: quadratic Bezier curves spread perpendicular
 * - Self-loops: cubic Bezier circles in the direction of max angular gap
 * - Directed arrows: binary subdivision to find node boundary intersection
 *
 * @par XML Attributes
 * - `graph` (optional): adjacency dictionary expression
 * - `layout` (optional): layout algorithm name
 * - `directed` (optional): "yes" for directed edges
 * - `labels` (optional): "yes" to show node labels
 * - `scale` (optional): layout scale factor (default 0.8)
 * - `rotate` (optional): layout rotation in degrees
 * - `seed` (optional): RNG seed for spring/random layouts
 * - `start` (required for bfs): root node handle
 * - `bipartite-set` (required for bipartite): node set expression
 * - `alignment` (optional for bipartite): "horizontal" or "vertical"
 * - `edge-stroke`, `edge-thickness`, `edge-dash`: edge styling
 * - `node-fill`, `node-stroke`, `node-thickness`, `node-style`, `node-size`: node styling
 * - `arrows` (optional): "middle" for mid-edge arrows
 * - `label-dictionary` (optional): mapping of handles to label text
 * - `loop-scale` (optional): global scale for self-loop size
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see Diagram::save_network_coordinates(), Diagram::get_network_coordinates()
 */
void network(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

// -- Layout algorithms --------------------------------------------------------

/// Simple adjacency list: node handle -> list of neighbor handles.
using AdjacencyList = std::map<std::string, std::vector<std::string>>;

/// Node position map: handle -> 2D position.
using PositionMap = std::map<std::string, Point2d>;

/**
 * @brief Circular layout: place nodes equally spaced on a unit circle.
 */
PositionMap circular_layout(const std::vector<std::string>& nodes);

/**
 * @brief Random layout: place nodes at random positions in [0,1]x[0,1].
 * @param seed Random seed.
 */
PositionMap random_layout(const std::vector<std::string>& nodes, int seed = 1);

/**
 * @brief Spring layout (Fruchterman-Reingold force-directed algorithm).
 * @param nodes List of node handles.
 * @param adj Adjacency list.
 * @param seed Random seed for initial positions.
 * @param iterations Number of iterations (default 50).
 */
PositionMap spring_layout(const std::vector<std::string>& nodes,
                          const AdjacencyList& adj,
                          int seed = 1, int iterations = 50);

/**
 * @brief BFS layout: levels from a root node, spread evenly per level.
 * @param nodes List of node handles.
 * @param adj Adjacency list.
 * @param start Root node handle.
 */
PositionMap bfs_layout(const std::vector<std::string>& nodes,
                       const AdjacencyList& adj,
                       const std::string& start);

/**
 * @brief Spectral layout: positions from Laplacian eigenvectors.
 */
PositionMap spectral_layout(const std::vector<std::string>& nodes,
                            const AdjacencyList& adj);

/**
 * @brief Planar layout using Boost.Graph Chrobak-Payne algorithm.
 * Falls back to spring layout if planar embedding fails.
 */
PositionMap planar_layout(const std::vector<std::string>& nodes,
                          const AdjacencyList& adj);

/**
 * @brief Bipartite layout: two columns of nodes.
 * @param nodes List of all node handles.
 * @param set1 Handles in the first partition.
 * @param alignment "horizontal" or "vertical".
 */
PositionMap bipartite_layout(const std::vector<std::string>& nodes,
                             const std::vector<std::string>& set1,
                             const std::string& alignment = "horizontal");

}  // namespace prefigure

#endif  // PREFIGURE_HAS_NETWORK
