#include "prefigure/implicit.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/user_namespace.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

namespace prefigure {

// Forward declaration
static void finish_outline_implicit(XmlNode element, Diagram& diagram, XmlNode parent);

// LevelSet: wraps f(x,y) - k
//
// We hold both a CompiledFunction2 (raw function pointer dispatch, no
// std::function shim, no Value variant copy/destroy at the boundary) and
// the original MathFunction2 as a fallback.  The fast path is taken when
// the user function is a registered scalar 2-arg user function -- which
// is the case for `<implicit-curve function="f"/>` in every example we've
// shipped.  Vector-valued or attribute-expression functions fall back to
// the std::function path automatically.
struct LevelSet {
    MathFunction2 f;                            // always set
    std::optional<CompiledFunction2> fast;      // set when scalar fast path is available
    double k;

    inline double value(const Point2d& p) const {
        if (fast) {
            // ~1 indirect call + the inline AST walk -- no Value variants,
            // no std::function machinery, no exception frames at all.
            return (*fast)(p[0], p[1]) - k;
        }
        Value vx(p[0]);
        Value vy(p[1]);
        return f(vx, vy).to_double() - k;
    }
};

// QuadTree for adaptive refinement
struct QuadTree {
    std::array<Point2d, 4> corners;  // BL, BR, TR, TL
    int depth;

    QuadTree(std::array<Point2d, 4> c, int d) : corners(c), depth(d) {}

    std::vector<QuadTree> subdivide() const {
        Point2d bottom = midpoint(Eigen::VectorXd(corners[0]), Eigen::VectorXd(corners[1]));
        Point2d left = midpoint(Eigen::VectorXd(corners[0]), Eigen::VectorXd(corners[3]));
        Point2d right = midpoint(Eigen::VectorXd(corners[1]), Eigen::VectorXd(corners[2]));
        Point2d top = midpoint(Eigen::VectorXd(corners[2]), Eigen::VectorXd(corners[3]));
        Point2d mid = midpoint(Eigen::VectorXd(bottom), Eigen::VectorXd(top));

        return {
            QuadTree({corners[0], Point2d(bottom), Point2d(mid), Point2d(left)}, depth - 1),
            QuadTree({Point2d(bottom), corners[1], Point2d(right), Point2d(mid)}, depth - 1),
            QuadTree({Point2d(left), Point2d(mid), Point2d(top), corners[3]}, depth - 1),
            QuadTree({Point2d(mid), Point2d(right), corners[2], Point2d(top)}, depth - 1)
        };
    }

    bool intersects(const LevelSet& g) const {
        double sign = g.value(corners[3]);
        for (int i = 0; i < 4; ++i) {
            double nextsign = g.value(corners[i]);
            if (sign * nextsign <= 0) return true;
            sign = nextsign;
        }
        return false;
    }

    static Point2d findzero(const Point2d& p1, const Point2d& p2, const LevelSet& g) {
        double dx_step = p2[0] - p1[0];
        double dy_step = p2[1] - p1[1];
        double change = 0.00001;
        double dx, dy, dt;
        if (dx_step != 0) {
            dx = change * std::abs(dx_step) / dx_step;
            dy = 0;
            dt = dx;
        } else {
            dy = change * std::abs(dy_step) / dy_step;
            dx = 0;
            dt = dy;
        }

        Point2d p = p1;
        double diff = 1;
        int N_iter = 0;
        while (std::abs(diff) > 1e-6 && N_iter < 50) {
            double fval = g.value(p);
            if (fval == 0) break;
            double df = (g.value(Point2d(p[0] + dx, p[1] + dy)) - fval) / dt;
            if (std::abs(df) < 1e-15) break;
            diff = fval / df;
            if (dx != 0) {
                p = Point2d(p[0] - diff, p[1]);
            } else {
                p = Point2d(p[0], p[1] - diff);
            }
            N_iter++;
        }
        return p;
    }

    struct Segment {
        Point2d start, end;
    };

    std::vector<Segment> segments(const LevelSet& g) const {
        Point2d corner = corners[3];
        double sign = g.value(corner);
        std::vector<Segment> segs;
        bool has_last_zero = false;
        Point2d last_zero;

        for (int i = 0; i < 4; ++i) {
            Point2d next_corner = corners[i];
            double nextsign = g.value(next_corner);
            if (sign == 0 && nextsign == 0) {
                segs.push_back({corner, next_corner});
            } else if (sign * nextsign <= 0) {
                if (!has_last_zero) {
                    last_zero = findzero(corner, next_corner, g);
                    has_last_zero = true;
                } else {
                    Point2d this_zero = findzero(corner, next_corner, g);
                    segs.push_back({last_zero, this_zero});
                    last_zero = this_zero;
                }
            }
            corner = next_corner;
            sign = nextsign;
        }
        return segs;
    }
};

void implicit_curve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline_implicit(element, diagram, parent);
        return;
    }

    if (diagram.output_format() == OutputFormat::Tactile) {
        element.attribute("stroke") ?
            element.attribute("stroke").set_value("black") :
            element.append_attribute("stroke").set_value("black");
    } else {
        set_attr(element, "stroke", "black");
    }
    set_attr(element, "thickness", "2");

    BBox bbox = diagram.bbox();

    // Get function.  We resolve two views in parallel:
    //   1. The MathFunction2 / std::function path (always available; works
    //      for vector-valued functions and inline expressions).
    //   2. A CompiledFunction2 raw-function-pointer view, available only
    //      when the attribute is a bare identifier referring to a registered
    //      scalar 2-arg user function.  This is the path the implicit
    //      example takes (`function="f"` where `f(x,y) = y^2 - x^3 + x`).
    //
    // The hot LevelSet::value loop prefers the compiled view when present
    // and falls back to the std::function path otherwise.  Both views are
    // resolved here once at element-handler time, so the inner loop never
    // touches the namespace map or pays for any lookup.
    MathFunction2 f;
    std::optional<CompiledFunction2> f_compiled;
    try {
        const char* func_attr = element.attribute("function").value();
        auto val = diagram.expr_ctx().eval(func_attr);
        if (val.is_function2()) {
            f = val.as_function2();
        } else if (val.is_function()) {
            spdlog::error("Error in <implicit-curve>: function must take two arguments");
            return;
        } else {
            spdlog::error("Error in <implicit-curve> retrieving function={}", func_attr);
            return;
        }
        // Try to get the raw-function-pointer view as well.  Returns
        // nullopt if the attribute isn't a bare identifier or if the
        // function isn't a registered scalar 2-arg user function.
        f_compiled = diagram.expr_ctx().get_compiled_scalar2(func_attr);
    } catch (...) {
        spdlog::error("Error in <implicit-curve> retrieving function={}",
                      get_attr(element, "function", ""));
        return;
    }

    double k;
    try {
        auto contour_attr = element.attribute("value");
        if (contour_attr) {
            k = diagram.expr_ctx().eval(contour_attr.value()).to_double();
        } else {
            k = diagram.expr_ctx().eval(get_attr(element, "k", "0")).to_double();
        }
    } catch (...) {
        k = 0.0;
    }

    int max_depth, initial_depth;
    try {
        max_depth = static_cast<int>(diagram.expr_ctx().eval(
            get_attr(element, "depth", "8")).to_double());
        initial_depth = static_cast<int>(diagram.expr_ctx().eval(
            get_attr(element, "initial-depth", "4")).to_double());
    } catch (...) {
        max_depth = 8;
        initial_depth = 4;
    }

    LevelSet levelset{f, f_compiled, k};

    // Build quad tree and find segments
    QuadTree root({
        Point2d(bbox[0], bbox[1]),
        Point2d(bbox[2], bbox[1]),
        Point2d(bbox[2], bbox[3]),
        Point2d(bbox[0], bbox[3])
    }, max_depth);

    // Initial uniform subdivision
    std::vector<QuadTree> tree = {root};
    for (int i = 0; i < initial_depth; ++i) {
        std::vector<QuadTree> newtree;
        for (const auto& node : tree) {
            auto children = node.subdivide();
            newtree.insert(newtree.end(), children.begin(), children.end());
        }
        tree = std::move(newtree);
    }

    // Adaptive refinement
    std::vector<QuadTree::Segment> all_segments;
    std::vector<QuadTree> work = std::move(tree);
    while (!work.empty()) {
        QuadTree node = work.front();
        work.erase(work.begin());

        if (node.depth == 0) {
            auto segs = node.segments(levelset);
            all_segments.insert(all_segments.end(), segs.begin(), segs.end());
        } else if (node.intersects(levelset)) {
            auto children = node.subdivide();
            work.insert(work.end(), children.begin(), children.end());
        }
    }

    // Build SVG path.  This used to assemble a std::vector<std::string> of
    // ~2N entries via per-segment pt2str() (4 std::format calls + 5 string
    // allocations per segment) and then join them into the final d-string in
    // a second loop.  Profiling implicit.xml showed pt2str + std::format at
    // ~9.4% of total time.  We now write directly into a single reserved
    // string with one std::format_to call per segment, eliminating ~3 of 4
    // allocations per segment and the entire intermediate vector.
    std::string d;
    // Each "M xxxx.x yyyy.y L xxxx.x yyyy.y " is at most ~40 chars; reserve
    // a generous slack so the back_inserter never reallocates.
    d.reserve(all_segments.size() * 48);
    auto out = std::back_inserter(d);
    for (const auto& s : all_segments) {
        Point2d s0 = diagram.transform(Point2d(s.start[0], s.start[1]));
        Point2d s1 = diagram.transform(Point2d(s.end[0], s.end[1]));
        if (!d.empty()) d += ' ';
        std::format_to(out, "M {:.1f} {:.1f} L {:.1f} {:.1f}",
                       s0[0], s0[1], s1[0], s1[1]);
    }

    XmlNode path = diagram.get_scratch().append_child("path");
    diagram.add_id(path, get_attr(element, "id", ""));
    diagram.register_svg_element(element, path);
    path.append_attribute("d").set_value(d.c_str());
    add_attr(path, get_1d_attr(element));

    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, path, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, path, parent);
        finish_outline_implicit(element, diagram, parent);
    } else {
        parent.append_copy(path);
    }
}

static void finish_outline_implicit(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

}  // namespace prefigure
