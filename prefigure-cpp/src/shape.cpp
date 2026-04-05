#include "prefigure/shape.hpp"

#ifdef PREFIGURE_HAS_SHAPES

#include "prefigure/diagram.hpp"
#include "prefigure/tags.hpp"
#include "prefigure/utilities.hpp"

#include <geos_c.h>
#include <spdlog/spdlog.h>

#include <cmath>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace prefigure {

// ---------------------------------------------------------------------------
// Allowed shape tags
// ---------------------------------------------------------------------------

static const std::unordered_set<std::string> allowed_shapes = {
    "arc", "area-between-curves", "area-under-curve", "circle",
    "ellipse", "graph", "parametric-curve", "path", "polygon",
    "rectangle", "shape", "spline"
};

// ---------------------------------------------------------------------------
// Forward declarations for internal helpers
// ---------------------------------------------------------------------------

static void finish_outline(XmlNode element, Diagram& diagram, XmlNode parent);

struct Point {
    double x, y;
};

static std::vector<Point> quad_bezier(Point p0, Point p1, Point p2, int N = 30);
static std::vector<Point> cubic_bezier(Point p0, Point p1, Point p2, Point p3, int N = 30);
static Point parse_coord(const std::string& s0, const std::string& s1);

// Build a GEOS geometry from an SVG path d-attribute string.
// If style == "linestring", creates LineStrings instead of Polygons.
static GEOSGeometry* build_geos_geom(GEOSContextHandle_t ctx,
                                      const std::string& path,
                                      const std::string& style = "");

// Convert a GEOS geometry back to an SVG path d-attribute string.
static std::string geos_to_svg_path(GEOSContextHandle_t ctx,
                                     const GEOSGeometry* geom);

// Clean up floating-point formatting in an SVG path string.
static std::string cleanup_str(const std::string& input);

// ---------------------------------------------------------------------------
// shape_define
// ---------------------------------------------------------------------------

void shape_define(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline)
        return;

    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        std::string tag = child.name();
        if (allowed_shapes.find(tag) == allowed_shapes.end()) {
            spdlog::error("In <define-shapes>, {} does not define a shape", tag);
            continue;
        }

        auto at_attr = child.attribute("at");
        if (at_attr) {
            std::string id = diagram.prepend_id_prefix(at_attr.value());
            child.remove_attribute("id");
            child.append_attribute("id") = id.c_str();
        }

        // Create a temporary parent to render the shape into
        auto scratch = diagram.get_scratch();
        auto dummy_parent = scratch.append_child("g");

        // Temporarily set format to SVG to avoid tactile overrides in shape defs
        // (matches Python: diagram.set_output_format('svg') ... restore)
        OutputFormat saved_format = diagram.output_format();
        diagram.set_output_format(OutputFormat::SVG);

        // Render the shape element
        tags::parse_element(child, diagram, dummy_parent, OutlineStatus::None);

        // Restore original output format
        diagram.set_output_format(saved_format);

        // Get the first child of the dummy parent as the rendered shape
        auto shape_node = dummy_parent.first_child();
        if (!shape_node) {
            spdlog::error("shape_define: rendering {} produced no output", tag);
            scratch.remove_child(dummy_parent);
            continue;
        }

        // Strip stroke and fill attributes (shapes are stored without styling)
        shape_node.remove_attribute("stroke");
        shape_node.remove_attribute("fill");

        diagram.add_shape(shape_node);
        scratch.remove_child(dummy_parent);
    }
}

// ---------------------------------------------------------------------------
// shape
// ---------------------------------------------------------------------------

void shape(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline(element, diagram, parent);
        return;
    }

    // Get the shape reference(s)
    std::string reference;
    auto shapes_attr = element.attribute("shapes");
    if (shapes_attr) {
        reference = shapes_attr.value();
    } else {
        auto shape_attr = element.attribute("shape");
        if (shape_attr) {
            reference = shape_attr.value();
        } else {
            spdlog::error("A <shape> tag needs a @shape or @shapes attribute");
            return;
        }
    }

    // Parse comma-separated shape references
    std::vector<std::string> shape_refs;
    {
        std::istringstream ss(reference);
        std::string token;
        while (std::getline(ss, token, ',')) {
            // Trim whitespace
            size_t start = token.find_first_not_of(" \t");
            size_t end = token.find_last_not_of(" \t");
            if (start != std::string::npos) {
                token = token.substr(start, end - start + 1);
            }
            shape_refs.push_back(diagram.prepend_id_prefix(token));
        }
    }

    // Recall each shape
    std::vector<XmlNode> shapes;
    for (auto& ref : shape_refs) {
        auto s = diagram.recall_shape(ref);
        if (!s) {
            spdlog::error("{} is not a reference to a shape", ref);
        } else {
            shapes.push_back(s);
        }
    }

    XmlNode path_node;
    std::string operation;
    auto op_attr = element.attribute("operation");
    if (op_attr) {
        operation = op_attr.value();
    }

    if (operation.empty()) {
        if (shapes.size() > 1) {
            operation = "union";
        } else if (shapes.size() == 1) {
            // Simple use reference
            path_node = parent.append_child("use");
            std::string href = "#" + diagram.prepend_id_prefix(reference);
            path_node.append_attribute("href") = href.c_str();
        } else {
            return;
        }
    }

    if (!operation.empty()) {
        // Extract SVG path d-attributes
        std::vector<std::string> paths;
        for (auto& s : shapes) {
            auto d_attr = s.attribute("d");
            if (d_attr) {
                paths.push_back(d_attr.value());
            }
        }

        if (paths.empty()) {
            spdlog::error("No valid paths found for shape operation");
            return;
        }

        std::string style;
        if (operation == "convex-hull") {
            style = "linestring";
        }

        spdlog::info("Applying shape operation {}", operation);

        // Initialize GEOS
        GEOSContextHandle_t ctx = GEOS_init_r();

        // Build geometries
        std::vector<GEOSGeometry*> geometries;
        for (size_t i = 0; i < paths.size(); ++i) {
            GEOSGeometry* geom = build_geos_geom(ctx, paths[i], style);
            if (!geom || !GEOSisValid_r(ctx, geom)) {
                spdlog::error("The shape {} is not a valid geometry", shape_refs[i]);
                spdlog::error("  Perhaps it is not defined by a simple curve");
                spdlog::error("  See the GEOS documentation for the operation: {}", operation);
                // Clean up
                for (auto* g : geometries) GEOSGeom_destroy_r(ctx, g);
                if (geom) GEOSGeom_destroy_r(ctx, geom);
                GEOS_finish_r(ctx);
                return;
            }
            geometries.push_back(geom);
        }

        GEOSGeometry* result = nullptr;

        if (operation == "intersection") {
            if (paths.size() < 2) {
                spdlog::error("Intersections require more than one shape");
                for (auto* g : geometries) GEOSGeom_destroy_r(ctx, g);
                GEOS_finish_r(ctx);
                return;
            }
            result = GEOSGeom_clone_r(ctx, geometries[0]);
            for (size_t i = 1; i < geometries.size(); ++i) {
                GEOSGeometry* tmp = GEOSIntersection_r(ctx, result, geometries[i]);
                GEOSGeom_destroy_r(ctx, result);
                result = tmp;
            }
        } else if (operation == "union") {
            if (paths.size() < 2) {
                spdlog::error("Unions require more than one shape");
                for (auto* g : geometries) GEOSGeom_destroy_r(ctx, g);
                GEOS_finish_r(ctx);
                return;
            }
            result = GEOSGeom_clone_r(ctx, geometries[0]);
            for (size_t i = 1; i < geometries.size(); ++i) {
                GEOSGeometry* tmp = GEOSUnion_r(ctx, result, geometries[i]);
                GEOSGeom_destroy_r(ctx, result);
                result = tmp;
            }
        } else if (operation == "difference") {
            if (paths.size() != 2) {
                spdlog::error("Differences require exactly two shapes");
                for (auto* g : geometries) GEOSGeom_destroy_r(ctx, g);
                GEOS_finish_r(ctx);
                return;
            }
            result = GEOSDifference_r(ctx, geometries[0], geometries[1]);
        } else if (operation == "symmetric-difference" || operation == "sym-diff") {
            if (paths.size() < 2) {
                spdlog::error("Symmetric differences require more than one shape");
                for (auto* g : geometries) GEOSGeom_destroy_r(ctx, g);
                GEOS_finish_r(ctx);
                return;
            }
            result = GEOSGeom_clone_r(ctx, geometries[0]);
            for (size_t i = 1; i < geometries.size(); ++i) {
                GEOSGeometry* tmp = GEOSSymDifference_r(ctx, result, geometries[i]);
                GEOSGeom_destroy_r(ctx, result);
                result = tmp;
            }
            operation = "symmetric difference";
        } else if (operation == "convex-hull") {
            // Union all geometries first, then convex hull
            GEOSGeometry* combined = GEOSGeom_clone_r(ctx, geometries[0]);
            for (size_t i = 1; i < geometries.size(); ++i) {
                GEOSGeometry* tmp = GEOSUnion_r(ctx, combined, geometries[i]);
                GEOSGeom_destroy_r(ctx, combined);
                combined = tmp;
            }
            result = GEOSConvexHull_r(ctx, combined);
            GEOSGeom_destroy_r(ctx, combined);
            operation = "convex hull";
        }

        // Clean up input geometries
        for (auto* g : geometries) GEOSGeom_destroy_r(ctx, g);

        if (!result || GEOSisEmpty_r(ctx, result)) {
            spdlog::warn("The {} defined by {} is empty", operation, reference);
            if (result) GEOSGeom_destroy_r(ctx, result);
            GEOS_finish_r(ctx);
            return;
        }

        // Convert result back to SVG path
        std::string d = geos_to_svg_path(ctx, result);
        GEOSGeom_destroy_r(ctx, result);
        GEOS_finish_r(ctx);

        path_node = parent.append_child("path");
        path_node.append_attribute("d") = d.c_str();
    }

    // Register the element
    auto id_attr = element.attribute("id");
    diagram.add_id(path_node, id_attr ? id_attr.value() : "");
    diagram.register_svg_element(element, path_node);

    // Apply styling
    if (diagram.output_format() == OutputFormat::Tactile) {
        if (element.attribute("stroke"))
            element.attribute("stroke").set_value("black");
        if (element.attribute("fill"))
            element.attribute("fill").set_value("lightgray");
    } else {
        set_attr(element, "stroke", "none");
        set_attr(element, "fill", "none");
    }

    set_attr(element, "thickness", "2");
    add_attr(path_node, get_2d_attr(element));
    cliptobbox(path_node, element, diagram);

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, path_node, parent);
        return;
    }

    std::string outline = get_attr(element, "outline", "no");
    if (outline == "yes" || diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, path_node, parent);
        finish_outline(element, diagram, parent);
    } else {
        parent.append_move(path_node);
    }
}

// ---------------------------------------------------------------------------
// finish_outline
// ---------------------------------------------------------------------------

static void finish_outline(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(
        element,
        element.attribute("stroke") ? element.attribute("stroke").value() : "none",
        element.attribute("thickness") ? element.attribute("thickness").value() : "2",
        get_attr(element, "fill", "none"),
        parent);
}

// ---------------------------------------------------------------------------
// SVG path parsing helpers
// ---------------------------------------------------------------------------

static Point parse_coord(const std::string& s0, const std::string& s1) {
    return {std::stod(s0), std::stod(s1)};
}

static std::vector<Point> quad_bezier(Point p0, Point p1, Point p2, int N) {
    std::vector<Point> points;
    points.reserve(N + 1);
    double dt = 1.0 / N;
    for (int i = 0; i <= N; ++i) {
        double t = i * dt;
        double s = 1.0 - t;
        double x = s * s * p0.x + 2 * t * s * p1.x + t * t * p2.x;
        double y = s * s * p0.y + 2 * t * s * p1.y + t * t * p2.y;
        points.push_back({x, y});
    }
    return points;
}

static std::vector<Point> cubic_bezier(Point p0, Point p1, Point p2, Point p3, int N) {
    std::vector<Point> points;
    points.reserve(N + 1);
    double dt = 1.0 / N;
    for (int i = 0; i <= N; ++i) {
        double t = i * dt;
        double s = 1.0 - t;
        double x = s * s * s * p0.x + 3 * t * s * s * p1.x
                 + 3 * t * t * s * p2.x + t * t * t * p3.x;
        double y = s * s * s * p0.y + 3 * t * s * s * p1.y
                 + 3 * t * t * s * p2.y + t * t * t * p3.y;
        points.push_back({x, y});
    }
    return points;
}

// ---------------------------------------------------------------------------
// Build GEOS geometry from SVG path
// ---------------------------------------------------------------------------

static GEOSGeometry* make_polygon_from_points(GEOSContextHandle_t ctx,
                                                const std::vector<Point>& pts) {
    if (pts.size() < 3) return nullptr;

    // Ensure the ring is closed
    bool closed = (std::abs(pts.front().x - pts.back().x) < 1e-10 &&
                   std::abs(pts.front().y - pts.back().y) < 1e-10);
    size_t n = pts.size() + (closed ? 0 : 1);

    GEOSCoordSequence* seq = GEOSCoordSeq_create_r(ctx, static_cast<unsigned>(n), 2);
    for (size_t i = 0; i < pts.size(); ++i) {
        GEOSCoordSeq_setXY_r(ctx, seq, static_cast<unsigned>(i), pts[i].x, pts[i].y);
    }
    if (!closed) {
        GEOSCoordSeq_setXY_r(ctx, seq, static_cast<unsigned>(pts.size()),
                              pts.front().x, pts.front().y);
    }

    GEOSGeometry* ring = GEOSGeom_createLinearRing_r(ctx, seq);
    if (!ring) return nullptr;
    return GEOSGeom_createPolygon_r(ctx, ring, nullptr, 0);
}

static GEOSGeometry* make_linestring_from_points(GEOSContextHandle_t ctx,
                                                   const std::vector<Point>& pts) {
    if (pts.size() < 2) return nullptr;

    GEOSCoordSequence* seq = GEOSCoordSeq_create_r(ctx, static_cast<unsigned>(pts.size()), 2);
    for (size_t i = 0; i < pts.size(); ++i) {
        GEOSCoordSeq_setXY_r(ctx, seq, static_cast<unsigned>(i), pts[i].x, pts[i].y);
    }
    return GEOSGeom_createLineString_r(ctx, seq);
}

static GEOSGeometry* build_geos_geom(GEOSContextHandle_t ctx,
                                      const std::string& path,
                                      const std::string& style) {
    bool as_linestring = (style == "linestring");

    // Tokenize the path string
    std::istringstream iss(path);
    std::vector<std::string> tokens;
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }

    std::vector<GEOSGeometry*> geom_parts;
    std::vector<Point> points;
    size_t i = 0;

    while (i < tokens.size()) {
        char cmd = tokens[i][0];
        if (cmd == 'M' || cmd == 'm' || cmd == 'L' || cmd == 'l') {
            ++i;
            if (i + 1 < tokens.size()) {
                points.push_back(parse_coord(tokens[i], tokens[i + 1]));
                i += 2;
            }
        } else if (cmd == 'Q' || cmd == 'q') {
            ++i;
            if (i + 4 <= tokens.size()) {
                Point p0 = points.back();
                Point p1 = parse_coord(tokens[i], tokens[i + 1]);
                Point p2 = parse_coord(tokens[i + 2], tokens[i + 3]);
                auto bez = quad_bezier(p0, p1, p2);
                points.insert(points.end(), bez.begin(), bez.end());
                i += 4;
            }
        } else if (cmd == 'C' || cmd == 'c') {
            ++i;
            if (i + 6 <= tokens.size()) {
                Point p0 = points.back();
                Point p1 = parse_coord(tokens[i], tokens[i + 1]);
                Point p2 = parse_coord(tokens[i + 2], tokens[i + 3]);
                Point p3 = parse_coord(tokens[i + 4], tokens[i + 5]);
                auto bez = cubic_bezier(p0, p1, p2, p3);
                points.insert(points.end(), bez.begin(), bez.end());
                i += 6;
            }
        } else if (cmd == 'Z' || cmd == 'z') {
            if (!points.empty()) {
                GEOSGeometry* part = as_linestring
                    ? make_linestring_from_points(ctx, points)
                    : make_polygon_from_points(ctx, points);
                if (part) geom_parts.push_back(part);
                points.clear();
            }
            ++i;
        } else {
            spdlog::warn("Unrecognized SVG path token {} when building GEOS geometry",
                         tokens[i]);
            ++i;
        }
    }

    // Handle unclosed path
    if (!points.empty()) {
        GEOSGeometry* part = as_linestring
            ? make_linestring_from_points(ctx, points)
            : make_polygon_from_points(ctx, points);
        if (part) geom_parts.push_back(part);
    }

    if (geom_parts.empty()) return nullptr;
    if (geom_parts.size() == 1) return geom_parts[0];

    // Create a multi-geometry
    if (as_linestring) {
        return GEOSGeom_createCollection_r(ctx, GEOS_MULTILINESTRING,
                                            geom_parts.data(),
                                            static_cast<unsigned>(geom_parts.size()));
    }
    return GEOSGeom_createCollection_r(ctx, GEOS_MULTIPOLYGON,
                                        geom_parts.data(),
                                        static_cast<unsigned>(geom_parts.size()));
}

// ---------------------------------------------------------------------------
// Convert GEOS geometry back to SVG path
// ---------------------------------------------------------------------------

static std::string ring_to_path(GEOSContextHandle_t ctx,
                                 const GEOSGeometry* ring) {
    std::ostringstream oss;
    const GEOSCoordSequence* seq = GEOSGeom_getCoordSeq_r(ctx, ring);
    unsigned n = 0;
    GEOSCoordSeq_getSize_r(ctx, seq, &n);

    for (unsigned i = 0; i < n; ++i) {
        double x, y;
        GEOSCoordSeq_getX_r(ctx, seq, i, &x);
        GEOSCoordSeq_getY_r(ctx, seq, i, &y);
        if (i == 0) {
            oss << "M " << float2str(x) << " " << float2str(y);
        } else {
            oss << " L " << float2str(x) << " " << float2str(y);
        }
    }
    oss << " Z";
    return oss.str();
}

static std::string polygon_to_path(GEOSContextHandle_t ctx,
                                    const GEOSGeometry* poly) {
    std::string result;

    // Exterior ring
    const GEOSGeometry* exterior = GEOSGetExteriorRing_r(ctx, poly);
    if (exterior) {
        result = ring_to_path(ctx, exterior);
    }

    // Interior rings (holes)
    int n_holes = GEOSGetNumInteriorRings_r(ctx, poly);
    for (int i = 0; i < n_holes; ++i) {
        const GEOSGeometry* hole = GEOSGetInteriorRingN_r(ctx, poly, i);
        if (hole) {
            result += " " + ring_to_path(ctx, hole);
        }
    }
    return result;
}

static std::string geos_to_svg_path(GEOSContextHandle_t ctx,
                                     const GEOSGeometry* geom) {
    int geom_type = GEOSGeomTypeId_r(ctx, geom);

    if (geom_type == GEOS_POLYGON) {
        return polygon_to_path(ctx, geom);
    }

    if (geom_type == GEOS_MULTIPOLYGON) {
        std::string result;
        int n = GEOSGetNumGeometries_r(ctx, geom);
        for (int i = 0; i < n; ++i) {
            const GEOSGeometry* part = GEOSGetGeometryN_r(ctx, geom, i);
            if (!result.empty()) result += " ";
            result += polygon_to_path(ctx, part);
        }
        return result;
    }

    // For other geometry types, try to extract coordinates directly
    if (geom_type == GEOS_LINESTRING || geom_type == GEOS_LINEARRING) {
        return ring_to_path(ctx, geom);
    }

    spdlog::warn("Unexpected GEOS geometry type {} in geos_to_svg_path", geom_type);
    return "";
}

// ---------------------------------------------------------------------------
// cleanup_str
// ---------------------------------------------------------------------------

static std::string cleanup_str(const std::string& input) {
    std::istringstream iss(input);
    std::ostringstream oss;
    std::string token;
    bool first = true;

    while (iss >> token) {
        // Split by commas too
        std::istringstream comma_ss(token);
        std::string sub;
        while (std::getline(comma_ss, sub, ',')) {
            if (sub.empty()) continue;
            if (!first) oss << " ";
            first = false;

            // Check if it contains a digit (numeric token)
            bool has_digit = false;
            for (char c : sub) {
                if (std::isdigit(c)) { has_digit = true; break; }
            }
            if (has_digit) {
                try {
                    double x = std::stod(sub);
                    oss << float2str(x);
                } catch (...) {
                    oss << sub;
                }
            } else {
                oss << sub;
            }
        }
    }
    return oss.str();
}

}  // namespace prefigure

#endif  // PREFIGURE_HAS_SHAPES
