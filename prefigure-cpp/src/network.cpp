#include "prefigure/network.hpp"

#ifdef PREFIGURE_HAS_NETWORK

#include "prefigure/coordinates.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/group.hpp"
#include "prefigure/label.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/point.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

// Boost.Graph (used for spectral layout eigenvalue decomposition)
// Planar layout via Chrobak-Payne is deferred — falls back to spring layout

namespace prefigure {

// ---------------------------------------------------------------------------
// Layout algorithm implementations
// ---------------------------------------------------------------------------

PositionMap circular_layout(const std::vector<std::string>& nodes) {
    PositionMap pos;
    int n = static_cast<int>(nodes.size());
    if (n == 0) return pos;
    if (n == 1) {
        pos[nodes[0]] = Point2d(0, 0);
        return pos;
    }
    double step = 2.0 * M_PI / n;
    for (int i = 0; i < n; ++i) {
        double angle = i * step;
        pos[nodes[i]] = Point2d(std::cos(angle), std::sin(angle));
    }
    return pos;
}

PositionMap random_layout(const std::vector<std::string>& nodes, int seed) {
    PositionMap pos;
    std::mt19937 rng(static_cast<unsigned>(seed));
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    for (auto& node : nodes) {
        pos[node] = Point2d(dist(rng), dist(rng));
    }
    return pos;
}

PositionMap spring_layout(const std::vector<std::string>& nodes,
                          const AdjacencyList& adj,
                          int seed, int iterations) {
    int n = static_cast<int>(nodes.size());
    if (n == 0) return {};

    // Build index map
    std::map<std::string, int> idx;
    for (int i = 0; i < n; ++i) idx[nodes[i]] = i;

    // Build edge set
    std::set<std::pair<int, int>> edges;
    for (auto& [node, neighbors] : adj) {
        auto it = idx.find(node);
        if (it == idx.end()) continue;
        int u = it->second;
        for (auto& nb : neighbors) {
            auto jt = idx.find(nb);
            if (jt == idx.end()) continue;
            int v = jt->second;
            if (u != v) {
                edges.insert({std::min(u, v), std::max(u, v)});
            }
        }
    }

    // Initialize positions randomly
    std::mt19937 rng(static_cast<unsigned>(seed));
    std::uniform_real_distribution<double> dist(-0.5, 0.5);

    std::vector<Point2d> pos(n);
    for (int i = 0; i < n; ++i) {
        pos[i] = Point2d(dist(rng), dist(rng));
    }

    // Fruchterman-Reingold (matching networkx spring_layout defaults)
    double k = 1.0 / std::sqrt(static_cast<double>(n));
    double t = 1.0 / std::sqrt(static_cast<double>(n));  // networkx uses W/10, scale-dependent

    for (int iter = 0; iter < iterations; ++iter) {
        std::vector<Point2d> disp(n, Point2d(0, 0));

        // Repulsive forces between all pairs
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                Point2d delta = pos[i] - pos[j];
                double d = delta.norm();
                if (d < 1e-10) d = 1e-10;
                double force = k * k / d;
                Point2d f = (delta / d) * force;
                disp[i] += f;
                disp[j] -= f;
            }
        }

        // Attractive forces along edges
        for (auto& [u, v] : edges) {
            Point2d delta = pos[u] - pos[v];
            double d = delta.norm();
            if (d < 1e-10) d = 1e-10;
            double force = d * d / k;
            Point2d f = (delta / d) * force;
            disp[u] -= f;
            disp[v] += f;
        }

        // Apply displacement capped by temperature (matching networkx)
        for (int i = 0; i < n; ++i) {
            double d = disp[i].norm();
            if (d > 1e-10) {
                pos[i] += (disp[i] / d) * std::min(d, t);
            }
        }

        t *= 0.95;  // Cool down (networkx uses 0.95)
    }

    // Build result
    PositionMap result;
    for (int i = 0; i < n; ++i) {
        result[nodes[i]] = pos[i];
    }
    return result;
}

PositionMap bfs_layout(const std::vector<std::string>& nodes,
                       const AdjacencyList& adj,
                       const std::string& start) {
    int n = static_cast<int>(nodes.size());
    if (n == 0) return {};

    // Build symmetric adjacency
    std::map<std::string, std::set<std::string>> sym_adj;
    for (auto& [node, neighbors] : adj) {
        for (auto& nb : neighbors) {
            if (node != nb) {
                sym_adj[node].insert(nb);
                sym_adj[nb].insert(node);
            }
        }
    }

    // BFS to assign levels
    std::map<std::string, int> level;
    std::queue<std::string> queue;
    queue.push(start);
    level[start] = 0;

    while (!queue.empty()) {
        std::string curr = queue.front();
        queue.pop();
        for (auto& nb : sym_adj[curr]) {
            if (level.find(nb) == level.end()) {
                level[nb] = level[curr] + 1;
                queue.push(nb);
            }
        }
    }

    // Assign levels to unreachable nodes
    int max_level = 0;
    for (auto& [k, v] : level) max_level = std::max(max_level, v);
    for (auto& node : nodes) {
        if (level.find(node) == level.end()) {
            level[node] = ++max_level;
        }
    }

    // Count nodes per level
    std::map<int, std::vector<std::string>> levels;
    for (auto& node : nodes) {
        levels[level[node]].push_back(node);
    }

    int num_levels = static_cast<int>(levels.size());
    PositionMap result;

    for (auto& [lev, lev_nodes] : levels) {
        int count = static_cast<int>(lev_nodes.size());
        for (int i = 0; i < count; ++i) {
            double x = (count > 1) ? static_cast<double>(i) / (count - 1) - 0.5 : 0.0;
            double y = (num_levels > 1) ? -static_cast<double>(lev) / (num_levels - 1) + 0.5 : 0.0;
            result[lev_nodes[i]] = Point2d(x, y);
        }
    }

    return result;
}

PositionMap spectral_layout(const std::vector<std::string>& nodes,
                            const AdjacencyList& adj) {
    int n = static_cast<int>(nodes.size());
    if (n <= 2) return circular_layout(nodes);

    // Build index map
    std::map<std::string, int> idx;
    for (int i = 0; i < n; ++i) idx[nodes[i]] = i;

    // Build Laplacian matrix (avoid double-counting edges)
    Eigen::MatrixXd L = Eigen::MatrixXd::Zero(n, n);
    std::set<std::pair<int,int>> seen_edges;
    for (auto& [node, neighbors] : adj) {
        auto it = idx.find(node);
        if (it == idx.end()) continue;
        int u = it->second;
        for (auto& nb : neighbors) {
            auto jt = idx.find(nb);
            if (jt == idx.end()) continue;
            int v = jt->second;
            if (u != v) {
                auto edge = std::make_pair(std::min(u,v), std::max(u,v));
                if (seen_edges.count(edge)) continue;
                seen_edges.insert(edge);
                L(u, v) -= 1;
                L(v, u) -= 1;
                L(u, u) += 1;
                L(v, v) += 1;
            }
        }
    }

    // Compute eigenvectors
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(L);
    auto eigenvalues = solver.eigenvalues();
    auto eigenvectors = solver.eigenvectors();

    // Use the 2nd and 3rd smallest eigenvectors (indices 1 and 2)
    // The smallest eigenvalue (index 0) is 0 (constant eigenvector)
    int idx_x = std::min(1, n - 1);
    int idx_y = std::min(2, n - 1);

    PositionMap result;
    for (int i = 0; i < n; ++i) {
        result[nodes[i]] = Point2d(eigenvectors(i, idx_x), eigenvectors(i, idx_y));
    }
    return result;
}

PositionMap planar_layout(const std::vector<std::string>& nodes,
                          const AdjacencyList& adj) {
    // Planar layout via Boost.Graph's Chrobak-Payne algorithm requires complex
    // template machinery (edge_index property maps, maximal planar augmentation).
    // For simplicity, fall back to spring layout which produces good results
    // for planar graphs.
    // TODO: Implement full Chrobak-Payne planar drawing if needed.
    spdlog::info("Planar layout requested, using spring layout as approximation");
    return spring_layout(nodes, adj);
}

PositionMap bipartite_layout(const std::vector<std::string>& nodes,
                             const std::vector<std::string>& set1,
                             const std::string& alignment) {
    std::set<std::string> s1(set1.begin(), set1.end());

    std::vector<std::string> group1, group2;
    for (auto& node : nodes) {
        if (s1.count(node))
            group1.push_back(node);
        else
            group2.push_back(node);
    }

    PositionMap result;
    bool horizontal = (alignment == "horizontal");

    auto layout_group = [&](const std::vector<std::string>& grp, double fixed_coord) {
        int count = static_cast<int>(grp.size());
        for (int i = 0; i < count; ++i) {
            double var_coord = (count > 1) ? static_cast<double>(i) / (count - 1) : 0.5;
            if (horizontal) {
                result[grp[i]] = Point2d(var_coord, fixed_coord);
            } else {
                result[grp[i]] = Point2d(fixed_coord, var_coord);
            }
        }
    };

    layout_group(group1, 0.0);
    layout_group(group2, 1.0);

    return result;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Parse a Python-style dict string like "{'A': ['B','C'], 'D': 'E'}"
// Returns a map from string keys to vectors of string values.
static std::map<std::string, std::vector<std::string>> parse_dict_string(const std::string& s) {
    std::map<std::string, std::vector<std::string>> result;

    // Strip outer whitespace
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) return result;
    std::string trimmed = s.substr(start, end - start + 1);

    // Strip outer braces
    if (trimmed.front() == '{') trimmed = trimmed.substr(1);
    if (trimmed.back() == '}') trimmed = trimmed.substr(0, trimmed.size() - 1);

    // Split by top-level commas (respecting brackets)
    // First split into key:value pairs
    std::vector<std::string> pairs;
    int bracket_depth = 0;
    std::string current;
    for (size_t i = 0; i < trimmed.size(); ++i) {
        char c = trimmed[i];
        if (c == '[') ++bracket_depth;
        else if (c == ']') --bracket_depth;
        else if (c == ',' && bracket_depth == 0) {
            // Check if this comma separates key:value pairs (not inside a list)
            // We need to figure out if we've seen a colon since the last split
            // A simple heuristic: if current contains ':', this is a pair boundary
            if (current.find(':') != std::string::npos) {
                pairs.push_back(current);
                current.clear();
                continue;
            }
            // Otherwise it's a comma inside a value that's not bracketed,
            // which shouldn't happen for well-formed dicts. But just in case,
            // keep accumulating.
        }
        current += c;
    }
    if (!current.empty()) pairs.push_back(current);

    // Helper to strip quotes and whitespace from a string
    auto strip = [](const std::string& str) -> std::string {
        size_t s = str.find_first_not_of(" \t\n\r'\"");
        size_t e = str.find_last_not_of(" \t\n\r'\"");
        if (s == std::string::npos) return "";
        return str.substr(s, e - s + 1);
    };

    for (auto& pair : pairs) {
        // Find the first colon that separates key from value
        size_t colon = pair.find(':');
        if (colon == std::string::npos) continue;

        std::string key = strip(pair.substr(0, colon));
        std::string value_str = pair.substr(colon + 1);

        // Trim value_str
        size_t vs = value_str.find_first_not_of(" \t\n\r");
        size_t ve = value_str.find_last_not_of(" \t\n\r");
        if (vs == std::string::npos) continue;
        value_str = value_str.substr(vs, ve - vs + 1);

        std::vector<std::string> values;
        if (value_str.front() == '[') {
            // It's a list: strip brackets and split by commas
            value_str = value_str.substr(1);
            if (value_str.back() == ']') value_str.pop_back();
            std::istringstream vss(value_str);
            std::string item;
            while (std::getline(vss, item, ',')) {
                std::string stripped = strip(item);
                if (!stripped.empty()) values.push_back(stripped);
            }
        } else {
            // Single value
            values.push_back(strip(value_str));
        }

        result[key] = values;
    }

    return result;
}

// Format a point as "(x,y)" string with high precision
static std::string fmt_pt(const Point2d& p) {
    return "(" + pt2long_str(p, ",") + ")";
}

// Format a point string for endpoints attribute
static std::string fmt_endpoints(const Point2d& p0, const Point2d& p1) {
    return fmt_pt(p0) + "," + fmt_pt(p1);
}

// ---------------------------------------------------------------------------
// network()
// ---------------------------------------------------------------------------

void network(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus outline_status) {
    // Handle finish_outline pass
    if (outline_status == OutlineStatus::FinishOutline) {
        // Retrieve stored coordinates element and delegate
        // (The element has already been converted to <coordinates> or <group>)
        auto coords_val = diagram.get_network_coordinates(
            element.attribute("name") ? element.attribute("name").value() : "");
        coordinates(element, diagram, parent, outline_status);
        return;
    }

    // Is the network directed?
    bool directed = (get_attr(element, "directed", "no") == "yes");

    // Global loop scale
    std::optional<Point2d> global_loop_scale;
    auto loop_scale_attr = element.attribute("loop-scale");
    if (loop_scale_attr) {
        auto val = diagram.expr_ctx().eval(loop_scale_attr.value());
        if (val.is_vector()) {
            global_loop_scale = val.as_point();
        }
    }

    // Label dictionary
    std::map<std::string, std::string> label_dictionary;
    auto label_dict_attr = element.attribute("label-dictionary");
    if (label_dict_attr) {
        auto parsed = parse_dict_string(label_dict_attr.value());
        for (auto& [k, v] : parsed) {
            if (!v.empty()) {
                label_dictionary[k] = v[0];
            }
        }
    }

    // Parse graph dictionary attribute
    std::map<std::string, std::vector<std::string>> graph_dict;
    auto graph_attr = element.attribute("graph");
    if (graph_attr) {
        graph_dict = parse_dict_string(graph_attr.value());
        if (graph_dict.empty()) {
            spdlog::error("@graph attribute of a <network> element should be a dictionary");
        }
    }

    // Edge tracking data structures
    // loops[node] = vector of edge elements (or null nodes for undecorated loops)
    std::map<std::string, std::vector<XmlNode>> loops;
    // directed_edges[(from, to)] = vector of edge elements
    std::map<std::pair<std::string, std::string>, std::vector<XmlNode>> directed_edges;
    // all_edges[(min, max)] = count of undirected edges
    std::map<std::pair<std::string, std::string>, int> all_edges;

    // Parse <node> children
    std::map<std::string, XmlNode> node_elements;
    PositionMap positions;

    for (auto node = element.child("node"); node; node = node.next_sibling("node")) {
        auto at_attr = node.attribute("at");
        if (!at_attr) continue;
        std::string handle = at_attr.value();
        node_elements[handle] = node;

        // Check for position
        auto p_attr = node.attribute("p");
        if (p_attr) {
            try {
                auto val = diagram.expr_ctx().eval(p_attr.value());
                positions[handle] = val.as_point();
            } catch (...) {
                spdlog::error("Error parsing position for node {}", handle);
            }
        }

        // Check for edges
        auto edges_attr = node.attribute("edges");
        if (edges_attr) {
            try {
                auto val = diagram.expr_ctx().eval(edges_attr.value());
                std::vector<std::string> edge_targets;

                if (val.is_vector()) {
                    auto& v = val.as_vector();
                    for (int i = 0; i < v.size(); ++i) {
                        edge_targets.push_back(std::format("{}", static_cast<int>(v[i])));
                    }
                } else if (val.is_double()) {
                    edge_targets.push_back(std::format("{}", static_cast<int>(val.as_double())));
                } else if (val.is_string()) {
                    edge_targets.push_back(val.as_string());
                }

                for (auto& dest : edge_targets) {
                    if (dest == handle) {
                        // Self-loop
                        loops[handle].push_back(XmlNode());
                        continue;
                    }
                    std::pair<std::string, std::string> dir_key = {handle, dest};
                    directed_edges[dir_key].push_back(XmlNode());

                    std::pair<std::string, std::string> undir_key;
                    if (handle < dest)
                        undir_key = {handle, dest};
                    else
                        undir_key = {dest, handle};
                    all_edges[undir_key]++;
                }
            } catch (...) {
                spdlog::error("Error parsing edges for node {}", handle);
            }
        }
    }

    // Process graph dictionary entries
    for (auto& [node, edge_list] : graph_dict) {
        if (node_elements.find(node) == node_elements.end()) {
            node_elements[node] = XmlNode();  // null node
        }
        for (auto& dest : edge_list) {
            if (dest == node) {
                loops[node].push_back(XmlNode());
                continue;
            }
            directed_edges[{node, dest}].push_back(XmlNode());

            std::pair<std::string, std::string> undir_key;
            if (node < dest) undir_key = {node, dest};
            else undir_key = {dest, node};
            all_edges[undir_key]++;
        }
    }

    // Process <edge> subelements
    for (auto edge = element.child("edge"); edge; edge = edge.next_sibling("edge")) {
        auto vert_attr = edge.attribute("vertices");
        if (!vert_attr) continue;

        try {
            auto val = diagram.expr_ctx().eval(vert_attr.value());
            if (!val.is_vector() || val.as_vector().size() < 2) {
                // Try as a pair of string identifiers
                spdlog::error("Error in <edge> evaluating vertices={}",
                              vert_attr.value());
                continue;
            }

            auto& v = val.as_vector();
            std::string p = std::format("{}", static_cast<int>(v[0]));
            std::string q = std::format("{}", static_cast<int>(v[1]));

            if (p == q) {
                // Self-loop
                bool placed = false;
                auto& loop_vec = loops[p];
                for (size_t i = 0; i < loop_vec.size(); ++i) {
                    if (!loop_vec[i]) {
                        loop_vec[i] = edge;
                        placed = true;
                        break;
                    }
                }
                if (!placed) {
                    loop_vec.push_back(edge);
                }
                continue;
            }

            // Try to place in existing directed_edges entry
            bool placed = false;
            auto dir_key = std::make_pair(p, q);
            if (directed_edges.count(dir_key)) {
                auto& edge_vec = directed_edges[dir_key];
                for (size_t i = 0; i < edge_vec.size(); ++i) {
                    if (!edge_vec[i]) {
                        edge_vec[i] = edge;
                        placed = true;
                        break;
                    }
                }
            }
            if (!placed && !directed) {
                auto rev_key = std::make_pair(q, p);
                if (directed_edges.count(rev_key)) {
                    auto& edge_vec = directed_edges[rev_key];
                    for (size_t i = 0; i < edge_vec.size(); ++i) {
                        if (!edge_vec[i]) {
                            edge_vec[i] = edge;
                            placed = true;
                            break;
                        }
                    }
                }
            }
            if (!placed) {
                directed_edges[dir_key].push_back(edge);
                std::pair<std::string, std::string> undir_key;
                if (p < q) undir_key = {p, q};
                else undir_key = {q, p};
                all_edges[undir_key]++;
            }
        } catch (...) {
            spdlog::error("Error parsing <edge> vertices");
        }
    }

    // Determine whether we need auto-layout
    bool auto_layout = (positions.size() != node_elements.size());
    CTM future_ctm;

    if (auto_layout) {
        // Collect all node keys
        std::vector<std::string> node_keys;
        for (auto& [k, _] : node_elements) node_keys.push_back(k);

        // Build adjacency list for layout algorithms
        AdjacencyList adj_list;
        for (auto& [key, edges] : directed_edges) {
            for (size_t i = 0; i < edges.size(); ++i) {
                adj_list[key.first].push_back(key.second);
            }
        }

        // Select and run layout algorithm
        std::string layout = get_attr(element, "layout", "spring");
        int seed = std::stoi(get_attr(element, "seed", "1"));

        if (layout == "spring" || layout.empty()) {
            positions = spring_layout(node_keys, adj_list, seed);
        } else if (layout == "bfs") {
            auto start_attr = element.attribute("start");
            if (!start_attr) {
                spdlog::error("bfs network layout needs a starting node");
                return;
            }
            positions = bfs_layout(node_keys, adj_list, start_attr.value());
        } else if (layout == "spectral") {
            positions = spectral_layout(node_keys, adj_list);
        } else if (layout == "circular") {
            positions = circular_layout(node_keys);
        } else if (layout == "random") {
            positions = random_layout(node_keys, seed);
        } else if (layout == "planar") {
            positions = planar_layout(node_keys, adj_list);
        } else if (layout == "bipartite") {
            std::string alignment = get_attr(element, "alignment", "horizontal");
            auto bp_attr = element.attribute("bipartite-set");
            if (!bp_attr) {
                spdlog::error("A bipartite network needs a @bipartite-set attribute");
                return;
            }
            std::vector<std::string> bipartite_set;
            try {
                auto val = diagram.expr_ctx().eval(bp_attr.value());
                if (val.is_vector()) {
                    auto& v = val.as_vector();
                    for (int i = 0; i < v.size(); ++i) {
                        bipartite_set.push_back(std::format("{}", static_cast<int>(v[i])));
                    }
                }
            } catch (...) {
                spdlog::error("Error parsing bipartite-set");
                return;
            }
            positions = bipartite_layout(node_keys, bipartite_set, alignment);
        }

        // Compute bounding box of layout positions
        double xmin = 1e30, xmax = -1e30, ymin = 1e30, ymax = -1e30;
        for (auto& [k, p] : positions) {
            xmin = std::min(xmin, p[0]);
            xmax = std::max(xmax, p[0]);
            ymin = std::min(ymin, p[1]);
            ymax = std::max(ymax, p[1]);
        }

        Point2d center(0.5 * (xmin + xmax), 0.5 * (ymin + ymax));

        double scale = std::stod(get_attr(element, "scale", "0.8"));
        double rotate_deg = std::stod(get_attr(element, "rotate", "0"));

        // Apply rotation around center
        CTM ctm;
        ctm.translate(-center[0], -center[1]);
        ctm.rotate(rotate_deg);
        for (auto& [k, p] : positions) {
            p = ctm.transform(p);
        }

        // Recenter
        xmin = 1e30; xmax = -1e30; ymin = 1e30; ymax = -1e30;
        for (auto& [k, p] : positions) {
            xmin = std::min(xmin, p[0]); xmax = std::max(xmax, p[0]);
            ymin = std::min(ymin, p[1]); ymax = std::max(ymax, p[1]);
        }
        center = Point2d(0.5 * (xmin + xmax), 0.5 * (ymin + ymax));

        CTM ctm2;
        ctm2.translate(-center[0], -center[1]);
        for (auto& [k, p] : positions) {
            p = ctm2.transform(p);
        }

        // Recompute bounds and scale
        xmin = 1e30; xmax = -1e30; ymin = 1e30; ymax = -1e30;
        for (auto& [k, p] : positions) {
            xmin = std::min(xmin, p[0]); xmax = std::max(xmax, p[0]);
            ymin = std::min(ymin, p[1]); ymax = std::max(ymax, p[1]);
        }

        double llx = xmin / scale;
        double lly = ymin / scale;
        double urx = xmax / scale;
        double ury = ymax / scale;

        // Avoid degenerate bounding boxes
        if (std::abs(urx - llx) < 1e-10) { llx -= 0.5; urx += 0.5; }
        if (std::abs(ury - lly) < 1e-10) { lly -= 0.5; ury += 0.5; }

        std::string bbox_str = std::format("({},{},{},{})",
                                            float2longstr(llx), float2longstr(lly),
                                            float2longstr(urx), float2longstr(ury));

        // Compute future CTM
        auto [diagram_ctm, diagram_bbox] = diagram.ctm_bbox();
        future_ctm = diagram_ctm.copy();
        future_ctm.translate(diagram_bbox[0], diagram_bbox[1]);
        future_ctm.scale(
            (diagram_bbox[2] - diagram_bbox[0]) / (urx - llx),
            (diagram_bbox[3] - diagram_bbox[1]) / (ury - lly));
        future_ctm.translate(-llx, -lly);

        // Convert element to <coordinates>
        // Remove all existing children and attributes except what we need
        while (element.first_child()) element.remove_child(element.first_child());
        element.set_name("coordinates");
        element.append_attribute("bbox") = bbox_str.c_str();
    } else {
        // No auto-layout: treat as a group
        while (element.first_child()) element.remove_child(element.first_child());
        element.set_name("group");
        future_ctm = diagram.ctm().copy();
    }

    // Read styling attributes
    std::string edge_stroke = get_attr(element, "edge-stroke", "black");
    std::string edge_thickness = get_attr(element, "edge-thickness", "2");
    std::string edge_dash = get_attr(element, "edge-dash", "none");
    std::string node_fill = get_attr(element, "node-fill", "darkorange");
    std::string node_stroke = get_attr(element, "node-stroke", "black");
    std::string node_thickness = get_attr(element, "node-thickness", "1");
    std::string node_style = get_attr(element, "node-style", "circle");
    bool labels = (get_attr(element, "labels", "no") == "yes");
    std::string default_node_size = labels ? "12" : "10";
    std::string node_size_str = get_attr(element, "node-size", default_node_size);
    bool mid_arrows = (get_attr(element, "arrows", "end") == "middle");

    if (diagram.output_format() == OutputFormat::Tactile) {
        node_fill = "white";
        node_stroke = "black";
    }

    double node_size_f = std::stod(node_size_str);
    double arrow_buffer = 3;
    double spread = 15;
    if (diagram.output_format() == OutputFormat::Tactile) {
        arrow_buffer = 12;
        spread = 20;
    }

    // Create edge group
    auto edge_group = element.append_child("group");
    edge_group.append_attribute("outline") = "tactile";

    // Track edge directions for each node (for loop orientation)
    std::map<std::string, std::vector<Point2d>> edge_directions;

    // --------------- Render edges ---------------
    for (auto& [edge_key, edges] : directed_edges) {
        std::string handle_0 = edge_key.first;
        std::string handle_1 = edge_key.second;

        std::pair<std::string, std::string> sorted_key;
        if (handle_0 < handle_1) sorted_key = {handle_0, handle_1};
        else sorted_key = {handle_1, handle_0};

        int total_edges = all_edges[sorted_key];
        double y = (total_edges - 1) / 2.0 * spread;

        for (size_t num = 0; num < edges.size(); ++num) {
            XmlNode edge_elem = edges[num];

            // Compute positions
            if (positions.find(handle_0) == positions.end() ||
                positions.find(handle_1) == positions.end()) {
                y -= spread;
                continue;
            }

            Point2d user_p0 = positions[handle_0];
            Point2d user_p1 = positions[handle_1];
            Point2d p0 = future_ctm.transform(user_p0);
            Point2d p1 = future_ctm.transform(user_p1);

            Point2d u_vec = p1 - p0;
            double angle = std::atan2(u_vec[1], u_vec[0]);
            double len = u_vec.norm();

            CTM edge_ctm;
            edge_ctm.translate(p0[0], p0[1]);
            edge_ctm.rotate(angle, "rad");

            Point2d center_pt = edge_ctm.transform(Point2d(len / 2.0, y));
            Point2d c1 = edge_ctm.transform(Point2d(len / 4.0, y));
            Point2d c2 = edge_ctm.transform(Point2d(3.0 * len / 4.0, y));

            center_pt = future_ctm.inverse_transform(center_pt);
            c1 = future_ctm.inverse_transform(c1);
            c2 = future_ctm.inverse_transform(c2);

            // Record edge directions for loop orientation
            edge_directions[handle_0].push_back(c1);
            edge_directions[handle_1].push_back(c2);

            // Create edge handle name
            std::string edge_handle = "edge-" + handle_0 + "-" + handle_1;
            if (edges.size() > 1) {
                edge_handle += "-" + std::to_string(num);
            }

            auto path = edge_group.append_child("path");
            path.append_attribute("at") = edge_handle.c_str();
            if (directed) {
                if (mid_arrows) {
                    path.append_attribute("mid-arrow") = "yes";
                } else {
                    path.append_attribute("arrows") = "1";
                }
            }

            // Apply styling from edge element or defaults
            if (!edge_elem) {
                path.append_attribute("stroke") = edge_stroke.c_str();
                path.append_attribute("thickness") = edge_thickness.c_str();
                if (edge_dash != "none")
                    path.append_attribute("dash") = edge_dash.c_str();
            } else {
                path.append_attribute("stroke") =
                    get_attr(edge_elem, "stroke", edge_stroke).c_str();
                path.append_attribute("thickness") =
                    get_attr(edge_elem, "thickness", edge_thickness).c_str();
                std::string d = get_attr(edge_elem, "dash", edge_dash);
                if (d != "none")
                    path.append_attribute("dash") = d.c_str();

                // Edge label: if the edge element has text or children
                bool has_edge_label = false;
                if (edge_elem.first_child()) {
                    has_edge_label = true;
                } else if (edge_elem.text()) {
                    std::string txt = edge_elem.text().get();
                    if (txt.find_first_not_of(" \t\n\r") != std::string::npos) {
                        has_edge_label = true;
                    }
                }
                if (has_edge_label) {
                    // Determine label anchor position
                    std::string label_loc_str = get_attr(edge_elem, "label-location", "0.5");
                    double label_location = 0.5;
                    try {
                        label_location = std::stod(label_loc_str);
                    } catch (...) {}

                    Point2d anchor;
                    if (std::abs(label_location - 0.5) < 1e-10) {
                        anchor = center_pt;
                    } else if (label_location < 0.5) {
                        std::vector<Eigen::VectorXd> ctrl = {
                            static_cast<Eigen::VectorXd>(user_p0),
                            static_cast<Eigen::VectorXd>(c1),
                            static_cast<Eigen::VectorXd>(center_pt)};
                        anchor = evaluate_bezier(ctrl, 2.0 * label_location);
                    } else {
                        std::vector<Eigen::VectorXd> ctrl = {
                            static_cast<Eigen::VectorXd>(center_pt),
                            static_cast<Eigen::VectorXd>(c2),
                            static_cast<Eigen::VectorXd>(user_p1)};
                        anchor = evaluate_bezier(ctrl, 2.0 * (label_location - 0.5));
                    }

                    Point2d direction = user_p1 - user_p0;
                    Point2d label_direction;
                    if (y >= 0) {
                        label_direction = rotate(direction, -M_PI / 2.0);
                    } else {
                        label_direction = rotate(direction, M_PI / 2.0);
                    }
                    std::string alignment = get_alignment_from_direction(label_direction);

                    // Create the label element as a copy of the edge element
                    auto label_el = edge_group.append_child("label");
                    for (auto child = edge_elem.first_child(); child; child = child.next_sibling()) {
                        label_el.append_copy(child);
                    }
                    if (edge_elem.text()) {
                        std::string txt = edge_elem.text().get();
                        if (txt.find_first_not_of(" \t\n\r") != std::string::npos) {
                            label_el.append_child(pugi::node_pcdata).set_value(txt.c_str());
                        }
                    }
                    if (!edge_elem.attribute("alignment")) {
                        label_el.append_attribute("alignment") = alignment.c_str();
                    }
                    label_el.append_attribute("anchor") = fmt_pt(anchor).c_str();
                }
            }

            // Check if this is a straight line (y offset near zero)
            if (std::abs(y) < 1e-10) {
                if (directed) {
                    path.set_name("line");
                    if (mid_arrows) {
                        path.append_attribute("endpoints") =
                            fmt_endpoints(user_p0, user_p1).c_str();
                        path.attribute("arrows").set_value("0");
                        path.append_attribute("additional-arrows") = "(0.5)";
                        y -= spread;
                        continue;
                    }

                    // Binary search for arrow endpoint at node boundary
                    Point2d seg0 = center_pt;
                    Point2d seg1 = user_p1;
                    for (int iter = 0; iter < 10; ++iter) {
                        Point2d mid = 0.5 * (seg0 + seg1);
                        auto node_el = node_elements.count(handle_1) ?
                            node_elements[handle_1] : XmlNode();
                        std::string end_style = node_el ?
                            get_attr(node_el, "style", node_style) : node_style;
                        if (inside(mid, user_p1, node_size_f, end_style,
                                   future_ctm, arrow_buffer)) {
                            seg1 = mid;
                        } else {
                            seg0 = mid;
                        }
                    }
                    path.append_attribute("endpoints") =
                        fmt_endpoints(user_p0, seg0).c_str();
                    y -= spread;
                    continue;
                }
            }

            // Curved edge (quadratic Bezier)
            path.append_attribute("start") = pt2long_str(user_p0, ",").c_str();

            // First half of the curve
            auto curveto1 = path.append_child("quadratic-bezier");
            std::string ctrl1 = fmt_pt(c1) + "," + fmt_pt(center_pt);
            curveto1.append_attribute("controls") = ctrl1.c_str();

            if (!directed || mid_arrows) {
                // Second half
                auto curveto2 = path.append_child("quadratic-bezier");
                std::string ctrl2 = fmt_pt(c2) + "," + fmt_pt(user_p1);
                curveto2.append_attribute("controls") = ctrl2.c_str();
            } else {
                // Directed: binary subdivision to find arrow endpoint
                std::vector<Point2d> current_curve = {center_pt, c2, user_p1};
                int N = 6;
                auto node_el = node_elements.count(handle_1) ?
                    node_elements[handle_1] : XmlNode();
                std::string end_style = node_el ?
                    get_attr(node_el, "style", node_style) : node_style;

                for (int iter = 0; iter < N; ++iter) {
                    Point2d cp0 = current_curve[0];
                    Point2d cp1 = current_curve[1];
                    Point2d cp2 = current_curve[2];
                    Point2d mc0 = 0.5 * (cp0 + cp1);
                    Point2d mc1 = 0.5 * (cp1 + cp2);
                    Point2d mid = 0.5 * (mc0 + mc1);

                    if (inside(mid, user_p1, node_size_f, end_style,
                               future_ctm, arrow_buffer)) {
                        current_curve = {cp0, mc0, mid};
                    } else {
                        current_curve = {mid, mc1, cp2};
                        auto curveto = path.append_child("quadratic-bezier");
                        std::string ctrl = fmt_pt(mc0) + "," + fmt_pt(mid);
                        curveto.append_attribute("controls") = ctrl.c_str();
                    }
                }
            }
            y -= spread;
        }
    }

    // --------------- Render self-loops ---------------
    for (auto& [node_handle, loop_record] : loops) {
        // Find direction for the loop
        double loop_angle = 0;
        double loop_gap = M_PI / 1.75;

        auto node_el = node_elements.count(node_handle) ?
            node_elements[node_handle] : XmlNode();

        std::string loop_orientation_str;
        if (node_el) {
            auto lo_attr = node_el.attribute("loop-orientation");
            if (lo_attr) loop_orientation_str = lo_attr.value();
        }

        auto dir_it = edge_directions.find(node_handle);
        if (dir_it == edge_directions.end() || !loop_orientation_str.empty()) {
            if (!loop_orientation_str.empty()) {
                loop_angle = -std::stod(loop_orientation_str) * M_PI / 180.0;
            }
            loop_gap = M_PI / 1.75;
        } else {
            // Compute direction angles and find maximum gap
            Point2d node_pos_svg = future_ctm.transform(positions[node_handle]);
            std::vector<double> angles;
            for (auto& dir : dir_it->second) {
                Point2d target_svg = future_ctm.transform(dir);
                Point2d diff = target_svg - node_pos_svg;
                angles.push_back(std::atan2(diff[1], diff[0]));
            }
            std::sort(angles.begin(), angles.end());
            angles.push_back(angles[0] + 2 * M_PI);

            double max_gap_val = 0;
            int max_gap_idx = 0;
            for (int i = 0; i < static_cast<int>(angles.size()) - 1; ++i) {
                double gap = angles[i + 1] - angles[i];
                if (gap > max_gap_val) {
                    max_gap_val = gap;
                    max_gap_idx = i;
                }
            }
            loop_angle = 0.5 * (angles[max_gap_idx + 1] + angles[max_gap_idx]);
            loop_gap = std::min(0.5 * max_gap_val, M_PI / 1.75);
        }

        Point2d node_position = positions[node_handle];
        Point2d P0 = future_ctm.transform(node_position);

        for (size_t j = 0; j < loop_record.size(); ++j) {
            XmlNode loop_elem = loop_record[j];

            CTM loop_ctm;
            loop_ctm.translate(P0[0], P0[1]);
            loop_ctm.rotate(loop_angle, "rad");

            double sc = (2.0 - 0.75 * j) * node_size_f;
            loop_ctm.scale(sc, sc);

            // Apply loop scale
            Point2d ls(1, 1);
            if (global_loop_scale) ls = *global_loop_scale;
            if (loop_elem) {
                auto lls_attr = loop_elem.attribute("loop-scale");
                if (lls_attr) {
                    try {
                        auto val = diagram.expr_ctx().eval(lls_attr.value());
                        if (val.is_vector()) ls = val.as_point();
                    } catch (...) {}
                }
            }
            loop_ctm.scale(ls[0], ls[1]);

            double alpha = 4.0 / 3.0;
            Point2d P1 = future_ctm.inverse_transform(loop_ctm.transform(Point2d(0, -alpha)));
            Point2d P2 = future_ctm.inverse_transform(loop_ctm.transform(Point2d(2, -alpha)));
            Point2d P3 = future_ctm.inverse_transform(loop_ctm.transform(Point2d(2, 0)));
            Point2d P4 = future_ctm.inverse_transform(loop_ctm.transform(Point2d(2, alpha)));
            Point2d P5 = future_ctm.inverse_transform(loop_ctm.transform(Point2d(0, alpha)));

            auto path = edge_group.append_child("path");
            std::string loop_handle = "loop-" + node_handle;
            if (loop_record.size() > 1) {
                loop_handle += "-" + std::to_string(j);
            }
            path.append_attribute("at") = loop_handle.c_str();
            path.append_attribute("start") = fmt_pt(node_position).c_str();
            if (directed) {
                if (mid_arrows) {
                    path.append_attribute("mid-arrow") = "yes";
                } else {
                    path.append_attribute("arrows") = "1";
                }
            }

            // Apply styling
            if (!loop_elem) {
                path.append_attribute("stroke") = edge_stroke.c_str();
                path.append_attribute("thickness") = edge_thickness.c_str();
                if (edge_dash != "none")
                    path.append_attribute("dash") = edge_dash.c_str();
            } else {
                path.append_attribute("stroke") =
                    get_attr(loop_elem, "stroke", edge_stroke).c_str();
                path.append_attribute("thickness") =
                    get_attr(loop_elem, "thickness", edge_thickness).c_str();
                std::string d = get_attr(loop_elem, "dash", edge_dash);
                if (d != "none")
                    path.append_attribute("dash") = d.c_str();
            }

            // Store loop curves for label positioning
            std::vector<Eigen::VectorXd> loop_curve0 = {
                static_cast<Eigen::VectorXd>(node_position),
                static_cast<Eigen::VectorXd>(P1),
                static_cast<Eigen::VectorXd>(P2),
                static_cast<Eigen::VectorXd>(P3)};
            std::vector<Eigen::VectorXd> loop_curve1 = {
                static_cast<Eigen::VectorXd>(P3),
                static_cast<Eigen::VectorXd>(P4),
                static_cast<Eigen::VectorXd>(P5),
                static_cast<Eigen::VectorXd>(node_position)};

            // Loop label
            if (loop_elem) {
                bool has_loop_label = false;
                if (loop_elem.first_child()) {
                    has_loop_label = true;
                } else if (loop_elem.text()) {
                    std::string txt = loop_elem.text().get();
                    if (txt.find_first_not_of(" \t\n\r") != std::string::npos) {
                        has_loop_label = true;
                    }
                }
                if (has_loop_label) {
                    double label_location = 0.5;
                    std::string ll_str = get_attr(loop_elem, "label-location", "0.5");
                    try { label_location = std::stod(ll_str); } catch (...) {}

                    Point2d anchor, anchor_ep;
                    if (label_location < 0.5) {
                        anchor = evaluate_bezier(loop_curve0, 2.0 * label_location);
                        anchor_ep = evaluate_bezier(loop_curve0, 2.0 * label_location + 0.0001);
                    } else {
                        anchor = evaluate_bezier(loop_curve1, 2.0 * (label_location - 0.5));
                        anchor_ep = evaluate_bezier(loop_curve1, 2.0 * (label_location + 0.0001 - 0.5));
                    }
                    Point2d direction = anchor_ep - anchor;
                    Point2d label_direction = rotate(direction, M_PI / 2.0);
                    std::string alignment = get_alignment_from_direction(label_direction);

                    auto label_el = edge_group.append_child("label");
                    for (auto child = loop_elem.first_child(); child; child = child.next_sibling()) {
                        label_el.append_copy(child);
                    }
                    if (loop_elem.text()) {
                        std::string txt = loop_elem.text().get();
                        if (txt.find_first_not_of(" \t\n\r") != std::string::npos) {
                            label_el.append_child(pugi::node_pcdata).set_value(txt.c_str());
                        }
                    }
                    if (!loop_elem.attribute("alignment")) {
                        label_el.append_attribute("alignment") = alignment.c_str();
                    }
                    label_el.append_attribute("anchor") = fmt_pt(anchor).c_str();
                }
            }

            // First cubic Bezier (outgoing arc)
            auto curveto1 = path.append_child("cubic-bezier");
            std::string ctrl1 = "(" + fmt_pt(P1) + "," + fmt_pt(P2) + "," + fmt_pt(P3) + ")";
            curveto1.append_attribute("controls") = ctrl1.c_str();

            if (!directed || mid_arrows) {
                // Returning arc
                auto curveto2 = path.append_child("cubic-bezier");
                std::string ctrl2 = "(" + fmt_pt(P4) + "," + fmt_pt(P5) + "," +
                                    fmt_pt(node_position) + ")";
                curveto2.append_attribute("controls") = ctrl2.c_str();
            } else {
                // Directed: binary subdivision on return arc
                std::vector<Point2d> current_curve = {P3, P4, P5, node_position};
                int N = 6;
                std::string end_style = node_el ?
                    get_attr(node_el, "style", node_style) : node_style;

                for (int iter = 0; iter < N; ++iter) {
                    Point2d cp0 = current_curve[0];
                    Point2d cp1 = current_curve[1];
                    Point2d cp2 = current_curve[2];
                    Point2d cp3 = current_curve[3];

                    Point2d p01 = 0.5 * (cp0 + cp1);
                    Point2d p12 = 0.5 * (cp1 + cp2);
                    Point2d p23 = 0.5 * (cp2 + cp3);
                    Point2d q1 = 0.5 * (p01 + p12);
                    Point2d q2 = 0.5 * (p12 + p23);
                    Point2d mid = 0.5 * (q1 + q2);

                    if (inside(mid, node_position, node_size_f, end_style,
                               future_ctm, arrow_buffer)) {
                        current_curve = {cp0, p01, q1, mid};
                    } else {
                        current_curve = {mid, q2, p23, cp3};
                        auto curveto = path.append_child("cubic-bezier");
                        std::string ctrl = fmt_pt(p01) + "," + fmt_pt(q1) + "," + fmt_pt(mid);
                        curveto.append_attribute("controls") = ctrl.c_str();
                    }
                }
            }
        }
    }

    // --------------- Render nodes ---------------
    auto node_group = element.append_child("group");
    node_group.append_attribute("outline") = "tactile";

    for (auto& [handle, position] : positions) {
        auto p_elem = node_group.append_child("point");
        p_elem.append_attribute("p") = fmt_pt(position).c_str();
        p_elem.append_attribute("size") = node_size_str.c_str();
        p_elem.append_attribute("at") = ("node-" + handle).c_str();

        auto node_el = node_elements.count(handle) ? node_elements[handle] : XmlNode();
        if (!node_el) {
            p_elem.append_attribute("fill") = node_fill.c_str();
            p_elem.append_attribute("stroke") = node_stroke.c_str();
            p_elem.append_attribute("thickness") = node_thickness.c_str();
            p_elem.append_attribute("style") = node_style.c_str();
        } else {
            p_elem.append_attribute("stroke") =
                get_attr(node_el, "stroke", node_stroke).c_str();
            p_elem.append_attribute("thickness") =
                get_attr(node_el, "thickness", node_thickness).c_str();
            p_elem.append_attribute("fill") =
                get_attr(node_el, "fill", node_fill).c_str();
            p_elem.append_attribute("style") =
                get_attr(node_el, "style", node_style).c_str();
        }

        if (labels) {
            // Check if node has its own label content
            bool has_label_content = false;
            if (node_el) {
                if (node_el.first_child() ||
                    (node_el.text() && std::string(node_el.text().get()).find_first_not_of(" \t\n\r") != std::string::npos)) {
                    has_label_content = true;
                }
            }

            XmlNode label_elem;
            if (has_label_content) {
                // Copy node as a label
                label_elem = node_group.append_child("label");
                // Copy children and text from node
                for (auto child = node_el.first_child(); child; child = child.next_sibling()) {
                    label_elem.append_copy(child);
                }
                if (node_el.text()) {
                    label_elem.append_child(pugi::node_pcdata).set_value(node_el.text().get());
                }
            } else {
                label_elem = node_group.append_child("label");
                auto m_elem = label_elem.append_child("m");
                std::string label_text = label_dictionary.count(handle) ?
                    label_dictionary[handle] : handle;
                m_elem.append_child(pugi::node_pcdata).set_value(label_text.c_str());
            }

            label_elem.append_attribute("p") = fmt_pt(position).c_str();
            label_elem.append_attribute("alignment") = "center";
            label_elem.append_attribute("offset") = "(0,0)";
            label_elem.append_attribute("clear-background") = "no";
        }
    }

    // Dispatch to coordinates or group for rendering
    if (auto_layout) {
        coordinates(element, diagram, parent, outline_status);
    } else {
        group(element, diagram, parent, outline_status);
    }
}

}  // namespace prefigure

#endif  // PREFIGURE_HAS_NETWORK
