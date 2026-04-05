#include "prefigure/diffeqs.hpp"

#ifdef PREFIGURE_HAS_DIFFEQS

#include "prefigure/arrow.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace prefigure {

static void finish_outline(XmlNode element, Diagram& diagram, XmlNode parent) {
    diagram.finish_outline(element,
                           element.attribute("stroke").value(),
                           element.attribute("thickness").value(),
                           get_attr(element, "fill", "none"),
                           parent);
}

// ---------------------------------------------------------------------------
// Dormand-Prince RK45 adaptive stepper (replaces Boost.Odeint dependency)
// ---------------------------------------------------------------------------

using OdeRhs = std::function<Eigen::VectorXd(double, const Eigen::VectorXd&)>;

// Dormand-Prince coefficients (same as scipy's RK45 / MATLAB ode45)
static constexpr double dp_a2  = 1.0/5.0;
static constexpr double dp_a3  = 3.0/10.0;
static constexpr double dp_a4  = 4.0/5.0;
static constexpr double dp_a5  = 8.0/9.0;

static constexpr double dp_b21 = 1.0/5.0;
static constexpr double dp_b31 = 3.0/40.0,     dp_b32 = 9.0/40.0;
static constexpr double dp_b41 = 44.0/45.0,    dp_b42 = -56.0/15.0,   dp_b43 = 32.0/9.0;
static constexpr double dp_b51 = 19372.0/6561.0, dp_b52 = -25360.0/2187.0,
                         dp_b53 = 64448.0/6561.0, dp_b54 = -212.0/729.0;
static constexpr double dp_b61 = 9017.0/3168.0, dp_b62 = -355.0/33.0,
                         dp_b63 = 46732.0/5247.0, dp_b64 = 49.0/176.0,
                         dp_b65 = -5103.0/18656.0;

// 5th-order solution weights
static constexpr double dp_c1 = 35.0/384.0,   dp_c3 = 500.0/1113.0,
                         dp_c4 = 125.0/192.0,  dp_c5 = -2187.0/6784.0,
                         dp_c6 = 11.0/84.0;

// 4th-order solution weights (for error estimate)
static constexpr double dp_d1 = 5179.0/57600.0,  dp_d3 = 7571.0/16695.0,
                         dp_d4 = 393.0/640.0,     dp_d5 = -92097.0/339200.0,
                         dp_d6 = 187.0/2100.0,    dp_d7 = 1.0/40.0;

// Integrate ODE from t0 to t_end, returning solution at N equally-spaced points.
// Uses adaptive stepping with dense output via interpolation.
static void rk45_solve(
    const OdeRhs& f,
    double t0, const Eigen::VectorXd& y0_in, double t_end, int N,
    double max_step,
    std::vector<double>& out_t,
    std::vector<Eigen::VectorXd>& out_y)
{
    double atol = 1e-6;
    double rtol = 1e-6;
    int dim = static_cast<int>(y0_in.size());

    // Generate evaluation times
    std::vector<double> t_eval(static_cast<size_t>(N));
    double divisor = (N > 1) ? static_cast<double>(N - 1) : 1.0;
    for (int i = 0; i < N; ++i) {
        t_eval[static_cast<size_t>(i)] = t0 + (t_end - t0) * i / divisor;
    }

    // Integrate adaptively, outputting at the t_eval points
    // Using cubic Hermite interpolation for dense output
    Eigen::VectorXd y = y0_in;
    double t = t0;
    double h = (t_end - t0) / N;  // initial step size
    if (max_step > 0 && h > max_step) h = max_step;

    // Store f(t,y) at the current point for Hermite interpolation
    Eigen::VectorXd f_curr = f(t, y);

    size_t eval_idx = 0;

    // Store the first point
    if (eval_idx < t_eval.size() && std::abs(t - t_eval[eval_idx]) < 1e-14) {
        out_t.push_back(t);
        out_y.push_back(y);
        ++eval_idx;
    }

    int max_iterations = 100000;
    int iter = 0;

    while (t < t_end - 1e-14 && eval_idx < t_eval.size() && iter < max_iterations) {
        ++iter;

        // Don't step past t_end
        if (t + h > t_end) h = t_end - t;
        if (max_step > 0 && h > max_step) h = max_step;
        if (h < 1e-15) break;

        // Compute RK45 stages (k1 = f_curr, already computed)
        Eigen::VectorXd k1 = f_curr;
        Eigen::VectorXd k2 = f(t + dp_a2 * h, y + h * dp_b21 * k1);
        Eigen::VectorXd k3 = f(t + dp_a3 * h, y + h * (dp_b31 * k1 + dp_b32 * k2));
        Eigen::VectorXd k4 = f(t + dp_a4 * h, y + h * (dp_b41 * k1 + dp_b42 * k2 + dp_b43 * k3));
        Eigen::VectorXd k5 = f(t + dp_a5 * h, y + h * (dp_b51 * k1 + dp_b52 * k2 + dp_b53 * k3 + dp_b54 * k4));
        Eigen::VectorXd k6 = f(t + h, y + h * (dp_b61 * k1 + dp_b62 * k2 + dp_b63 * k3 + dp_b64 * k4 + dp_b65 * k5));

        // 5th-order solution
        Eigen::VectorXd y_new = y + h * (dp_c1 * k1 + dp_c3 * k3 + dp_c4 * k4 + dp_c5 * k5 + dp_c6 * k6);

        // 4th-order solution for error estimate
        Eigen::VectorXd k7 = f(t + h, y_new);
        Eigen::VectorXd y4 = y + h * (dp_d1 * k1 + dp_d3 * k3 + dp_d4 * k4 + dp_d5 * k5 + dp_d6 * k6 + dp_d7 * k7);

        // Error estimate
        Eigen::VectorXd err = y_new - y4;
        double err_norm = 0.0;
        for (int i = 0; i < dim; ++i) {
            double sc = atol + rtol * std::max(std::abs(y[i]), std::abs(y_new[i]));
            err_norm += (err[i] / sc) * (err[i] / sc);
        }
        err_norm = std::sqrt(err_norm / dim);

        if (err_norm <= 1.0) {
            // Step accepted
            double t_prev = t;
            Eigen::VectorXd y_prev = y;
            Eigen::VectorXd f_prev = f_curr;

            t += h;
            y = y_new;
            // f at the new point: use FSAL property (k7 = f(t+h, y_new))
            f_curr = k7;

            // Output any evaluation points we've passed using Hermite interpolation
            while (eval_idx < t_eval.size() && t_eval[eval_idx] <= t + 1e-14) {
                if (std::abs(t - t_eval[eval_idx]) < 1e-14) {
                    out_t.push_back(t);
                    out_y.push_back(y);
                } else {
                    // Cubic Hermite interpolation between (t_prev, y_prev) and (t, y)
                    // using derivatives f_prev and f_curr
                    double step_h = t - t_prev;
                    double s = (t_eval[eval_idx] - t_prev) / step_h;
                    Eigen::VectorXd dy = y - y_prev;
                    Eigen::VectorXd y_interp = (1.0 - s) * y_prev + s * y
                        + s * (s - 1.0) * ((1.0 - 2.0 * s) * dy
                            + step_h * ((s - 1.0) * f_prev + s * f_curr));
                    out_t.push_back(t_eval[eval_idx]);
                    out_y.push_back(y_interp);
                }
                ++eval_idx;
            }

            // Adjust step size
            double factor = 0.9 * std::pow(err_norm, -0.2);
            factor = std::min(5.0, std::max(0.2, factor));
            h *= factor;
        } else {
            // Step rejected; reduce step size
            double factor = 0.9 * std::pow(err_norm, -0.25);
            factor = std::max(0.2, factor);
            h *= factor;
        }

        if (max_step > 0 && h > max_step) h = max_step;
    }

    // Make sure we have the final point
    if (eval_idx < t_eval.size()) {
        // Fill remaining evaluation points with the last computed value
        while (eval_idx < t_eval.size()) {
            out_t.push_back(t_eval[eval_idx]);
            out_y.push_back(y);
            ++eval_idx;
        }
    }
}

// ---------------------------------------------------------------------------
// de_solve
// ---------------------------------------------------------------------------

void de_solve(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)parent;

    if (status == OutlineStatus::FinishOutline) {
        return;
    }

    // Retrieve the ODE right-hand side function
    MathFunction2 f2;
    try {
        auto val = diagram.expr_ctx().eval(element.attribute("function").value());
        if (val.is_function2()) {
            f2 = val.as_function2();
        } else {
            spdlog::error("Error in ODE solver: cannot retrieve function={}",
                          element.attribute("function").value());
            return;
        }
    } catch (...) {
        spdlog::error("Error in ODE solver: cannot retrieve function={}",
                      get_attr(element, "function", ""));
        return;
    }

    // Retrieve t0
    double t0;
    try {
        t0 = diagram.expr_ctx().eval(element.attribute("t0").value()).to_double();
    } catch (...) {
        spdlog::error("Error in ODE solver: cannot retrieve t0={}",
                      get_attr(element, "t0", ""));
        return;
    }

    // Retrieve y0
    Eigen::VectorXd y0;
    try {
        auto y0_val = diagram.expr_ctx().eval(element.attribute("y0").value());
        if (y0_val.is_double()) {
            y0.resize(1);
            y0[0] = y0_val.as_double();
        } else if (y0_val.is_vector()) {
            y0 = y0_val.as_vector();
        } else {
            spdlog::error("Error in ODE solver: cannot retrieve y0={}",
                          get_attr(element, "y0", ""));
            return;
        }
    } catch (...) {
        spdlog::error("Error in ODE solver: cannot retrieve y0={}",
                      get_attr(element, "y0", ""));
        return;
    }

    // t1 defaults to bbox x_max
    double t1 = diagram.bbox()[2];
    if (element.attribute("t1")) {
        try {
            t1 = diagram.expr_ctx().eval(element.attribute("t1").value()).to_double();
        } catch (...) {
            spdlog::error("Error in ODE solver: cannot retrieve t1={}",
                          get_attr(element, "t1", ""));
        }
    }

    int N = 100;
    if (element.attribute("N")) {
        try {
            N = static_cast<int>(diagram.expr_ctx().eval(
                element.attribute("N").value()).to_double());
        } catch (...) {}
    }

    // max-step (optional)
    double max_step = -1.0;
    if (element.attribute("max-step")) {
        try {
            max_step = diagram.expr_ctx().eval(
                element.attribute("max-step").value()).to_double();
        } catch (...) {}
    }

    // Check method attribute
    std::string method = get_attr(element, "method", "RK45");
    if (method != "RK45" && method != "DOP853") {
        spdlog::warn("ODE method '{}' not supported in C++ backend, using RK45", method);
    }

    int dim = static_cast<int>(y0.size());

    // Wrap MathFunction2 as an OdeRhs lambda
    OdeRhs rhs = [&f2](double t, const Eigen::VectorXd& y) -> Eigen::VectorXd {
        Value result = f2(Value(t), Value(y));
        if (result.is_vector()) {
            return result.as_vector();
        } else if (result.is_double()) {
            Eigen::VectorXd v(1);
            v[0] = result.as_double();
            return v;
        }
        return Eigen::VectorXd::Zero(y.size());
    };

    // Find discontinuities (breaks) in the ODE RHS
    std::vector<double> breaks = diagram.expr_ctx().find_breaks(f2, t0, Value(y0));
    {
        std::vector<double> filtered;
        for (double b : breaks) {
            if (b >= t0 && b < t1) {
                filtered.push_back(b);
            }
        }
        breaks = std::move(filtered);
    }
    std::sort(breaks.begin(), breaks.end());
    breaks.push_back(t1);

    // Handle break at t0
    if (!breaks.empty() && std::abs(t0 - breaks[0]) < 1e-10) {
        Value jump = diagram.expr_ctx().measure_de_jump(f2, t0, Value(y0));
        if (jump.is_vector()) {
            y0 += jump.as_vector();
        } else if (jump.is_double()) {
            y0[0] += jump.as_double();
        }
        breaks.erase(breaks.begin());
    }

    // Accumulate solution pieces
    std::vector<double> all_t;
    std::vector<Eigen::VectorXd> all_y;

    while (!breaks.empty()) {
        double next_t = breaks.front();
        breaks.erase(breaks.begin());

        std::vector<double> seg_t;
        std::vector<Eigen::VectorXd> seg_y;

        rk45_solve(rhs, t0, y0, next_t, N, max_step, seg_t, seg_y);

        // Append to accumulated solution
        for (size_t i = 0; i < seg_t.size(); ++i) {
            all_t.push_back(seg_t[i]);
            all_y.push_back(seg_y[i]);
        }

        // Update t0, y0 for next segment
        t0 = next_t;
        if (!seg_y.empty()) {
            y0 = seg_y.back();
        }

        // Apply jump at the break
        Value jump = diagram.expr_ctx().measure_de_jump(f2, t0, Value(y0));
        if (jump.is_vector()) {
            y0 += jump.as_vector();
        } else if (jump.is_double()) {
            y0[0] += jump.as_double();
        }
    }

    // Build the solution matrix: rows = [t, y0, y1, ...], cols = time points
    // This matches Python's np.stack((solution_t, *solution_y)) layout
    int n_pts = static_cast<int>(all_t.size());
    if (n_pts == 0) {
        spdlog::error("Error in ODE solver: no solution points generated");
        return;
    }

    Eigen::MatrixXd solution(1 + dim, n_pts);
    for (int i = 0; i < n_pts; ++i) {
        solution(0, i) = all_t[static_cast<size_t>(i)];
        for (int j = 0; j < dim; ++j) {
            solution(j + 1, i) = all_y[static_cast<size_t>(i)][j];
        }
    }

    // Store in namespace
    std::string name = get_attr(element, "name", "");
    if (name.empty()) {
        spdlog::error("Error in ODE solver: name attribute is required");
        return;
    }
    diagram.expr_ctx().enter_namespace(name, Value(solution));
}

// ---------------------------------------------------------------------------
// plot_de_solution
// ---------------------------------------------------------------------------

void plot_de_solution(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::FinishOutline) {
        finish_outline(element, diagram, parent);
        return;
    }

    // Either solve inline or get a previously computed solution
    Eigen::MatrixXd solution;
    if (element.attribute("function")) {
        // Solve inline: set a temporary name and call de_solve
        if (!element.attribute("name")) {
            element.append_attribute("name").set_value("__de_solution");
        } else {
            element.attribute("name").set_value("__de_solution");
        }
        de_solve(element, diagram, parent, OutlineStatus::None);
        try {
            auto val = diagram.expr_ctx().eval("__de_solution");
            if (val.is_matrix()) {
                solution = val.as_matrix();
            } else {
                spdlog::error("Error in <plot-de-solution>: inline solve did not produce a matrix");
                return;
            }
        } catch (...) {
            spdlog::error("Error in <plot-de-solution>: inline solve failed");
            return;
        }
    } else {
        try {
            auto val = diagram.expr_ctx().eval(
                element.attribute("solution").value());
            if (val.is_matrix()) {
                solution = val.as_matrix();
            } else {
                spdlog::error("Error in <plot-de-solution> finding solution={}",
                              get_attr(element, "solution", ""));
                return;
            }
        } catch (...) {
            spdlog::error("Error in <plot-de-solution> finding solution={}",
                          get_attr(element, "solution", ""));
            return;
        }
    }

    // Parse axes specification: "(t,y)" or "(y0,y1)" etc.
    std::string axes_str = get_attr(element, "axes", "(t,y)");
    // Strip leading/trailing whitespace, parens, brackets
    while (!axes_str.empty() &&
           (axes_str.front() == ' ' || axes_str.front() == '(' || axes_str.front() == '[')) {
        axes_str.erase(axes_str.begin());
    }
    while (!axes_str.empty() &&
           (axes_str.back() == ' ' || axes_str.back() == ')' || axes_str.back() == ']')) {
        axes_str.pop_back();
    }

    // Split on comma
    std::string x_axis, y_axis;
    auto comma_pos = axes_str.find(',');
    if (comma_pos != std::string::npos) {
        x_axis = axes_str.substr(0, comma_pos);
        y_axis = axes_str.substr(comma_pos + 1);
    } else {
        spdlog::error("Error in <plot-de-solution> setting axes={}",
                      get_attr(element, "axes", "(t,y)"));
        return;
    }
    // Trim whitespace
    while (!x_axis.empty() && x_axis.front() == ' ') x_axis.erase(x_axis.begin());
    while (!x_axis.empty() && x_axis.back() == ' ') x_axis.pop_back();
    while (!y_axis.empty() && y_axis.front() == ' ') y_axis.erase(y_axis.begin());
    while (!y_axis.empty() && y_axis.back() == ' ') y_axis.pop_back();

    // Map axis names to solution row indices
    // 't' -> row 0, 'y' or 'y0' -> row 1, 'y1' -> row 2, etc.
    auto axis_to_row = [](const std::string& name) -> int {
        if (name == "t") return 0;
        if (name == "y" || name == "y0") return 1;
        if (name.size() > 1 && name[0] == 'y') {
            try {
                return std::stoi(name.substr(1)) + 1;
            } catch (...) {
                return -1;
            }
        }
        return -1;
    };

    int x_row = axis_to_row(x_axis);
    int y_row = axis_to_row(y_axis);

    if (x_row < 0 || y_row < 0 ||
        x_row >= solution.rows() || y_row >= solution.rows()) {
        spdlog::error("Error in <plot-de-solution> invalid axes specification");
        return;
    }

    int n_pts = static_cast<int>(solution.cols());

    // Build SVG path commands
    std::vector<std::string> cmds;
    {
        Point2d p = diagram.transform(Point2d(solution(x_row, 0),
                                               solution(y_row, 0)));
        cmds.push_back("M " + pt2str(p));
    }
    for (int i = 1; i < n_pts; ++i) {
        Point2d p = diagram.transform(Point2d(solution(x_row, i),
                                               solution(y_row, i)));
        cmds.push_back("L " + pt2str(p));
    }

    // Default styling
    if (diagram.output_format() == OutputFormat::Tactile) {
        if (element.attribute("stroke")) {
            element.attribute("stroke").set_value("black");
        } else {
            element.append_attribute("stroke").set_value("black");
        }
    } else {
        set_attr(element, "stroke", "blue");
        set_attr(element, "fill", "none");
    }
    set_attr(element, "thickness", "2");

    // Create SVG path element
    XmlNode path = diagram.get_scratch().append_child("path");
    diagram.add_id(path, get_attr(element, "id", ""));
    diagram.register_svg_element(element, path);
    add_attr(path, get_2d_attr(element));

    // Handle arrows
    if (get_attr(element, "arrow", "no") == "yes") {
        std::string aw = get_attr(element, "arrow-width", "");
        std::string aa = get_attr(element, "arrow-angles", "");
        add_arrowhead_to_path(diagram, "marker-end", path, aw, aa);

        // Arrow in the middle of trajectory at a specific t value
        std::string arrow_loc_str = get_attr(element, "arrow-location", "");
        if (!arrow_loc_str.empty()) {
            double arrow_location = 0.0;
            try {
                arrow_location = diagram.expr_ctx().eval(arrow_loc_str).to_double();
            } catch (...) {
                goto after_arrow;
            }

            {
                // t values are in row 0
                double t_first = solution(0, 0);
                double t_last = solution(0, n_pts - 1);

                if (arrow_location > t_first && arrow_location < t_last) {
                    // Find the index where arrow_location falls
                    int index = 0;
                    for (int i = 0; i < n_pts; ++i) {
                        if (arrow_location <= solution(0, i)) {
                            index = i;
                            break;
                        }
                    }

                    // Build a sub-path from (index - 5) to index
                    int start = std::max(index - 5, 0);
                    Point2d p = diagram.transform(
                        Point2d(solution(x_row, start), solution(y_row, start)));
                    cmds.push_back("M " + pt2str(p));
                    for (int i = start + 1; i <= index; ++i) {
                        p = diagram.transform(
                            Point2d(solution(x_row, i), solution(y_row, i)));
                        cmds.push_back("L " + pt2str(p));
                    }
                }
            }
        }
    }

after_arrow:
    // Join path commands
    std::string d;
    for (const auto& s : cmds) {
        if (!d.empty()) d += " ";
        d += s;
    }
    path.append_attribute("d").set_value(d.c_str());

    // Clip to bbox
    if (!element.attribute("cliptobbox")) {
        element.append_attribute("cliptobbox").set_value("yes");
    }
    cliptobbox(path, element, diagram);

    // Outline handling
    if (status == OutlineStatus::AddOutline) {
        diagram.add_outline(element, path, parent);
        return;
    }

    if (get_attr(element, "outline", "no") == "yes" ||
        diagram.output_format() == OutputFormat::Tactile) {
        diagram.add_outline(element, path, parent);
        finish_outline(element, diagram, parent);
    } else {
        parent.append_copy(path);
    }
}

}  // namespace prefigure

#endif  // PREFIGURE_HAS_DIFFEQS
