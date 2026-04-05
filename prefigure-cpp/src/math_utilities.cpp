#include "prefigure/math_utilities.hpp"
#include "prefigure/calculus.hpp"
#include "prefigure/diagram.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace prefigure {

static Diagram* g_diagram = nullptr;

void math_set_diagram(Diagram* d) {
    g_diagram = d;
}

Diagram* math_get_diagram() {
    return g_diagram;
}

// --- Vector operations ---

double dot(const Eigen::VectorXd& u, const Eigen::VectorXd& v) {
    return u.dot(v);
}

double distance(const Eigen::VectorXd& p, const Eigen::VectorXd& q) {
    return (p - q).norm();
}

double length(const Eigen::VectorXd& u) {
    return u.norm();
}

Eigen::VectorXd normalize(const Eigen::VectorXd& u) {
    double len = u.norm();
    if (len < 1e-15) {
        throw std::runtime_error("Cannot normalize zero-length vector");
    }
    return u / len;
}

Eigen::VectorXd midpoint(const Eigen::VectorXd& u, const Eigen::VectorXd& v) {
    return 0.5 * (u + v);
}

double angle(const Eigen::VectorXd& p, const std::string& units) {
    double a = std::atan2(p[1], p[0]);
    if (units == "deg") {
        return a * 180.0 / M_PI;
    }
    return a;
}

Eigen::VectorXd rotate(const Eigen::VectorXd& v, double theta) {
    double c = std::cos(theta);
    double s = std::sin(theta);
    Eigen::VectorXd result(2);
    result[0] = c * v[0] - s * v[1];
    result[1] = s * v[0] + c * v[1];
    return result;
}

Eigen::VectorXd roll(const Eigen::VectorXd& array) {
    if (array.size() <= 1) return array;
    Eigen::VectorXd result(array.size());
    result[0] = array[array.size() - 1];
    result.segment(1, array.size() - 1) = array.head(array.size() - 1);
    return result;
}

double choose(double n, double k) {
    int ni = static_cast<int>(n);
    int ki = static_cast<int>(k);
    if (ki < 0 || ki > ni) return 0.0;
    if (ki == 0 || ki == ni) return 1.0;
    // Use the multiplicative formula for binomial coefficient
    double result = 1.0;
    for (int i = 0; i < std::min(ki, ni - ki); ++i) {
        result = result * (ni - i) / (i + 1);
    }
    return result;
}

// --- Indicator functions ---

double chi_oo(double a, double b, double t) {
    return (t > a && t < b) ? 1.0 : 0.0;
}

double chi_oc(double a, double b, double t) {
    return (t > a && t <= b) ? 1.0 : 0.0;
}

double chi_co(double a, double b, double t) {
    return (t >= a && t < b) ? 1.0 : 0.0;
}

double chi_cc(double a, double b, double t) {
    return (t >= a && t <= b) ? 1.0 : 0.0;
}

// --- Calculus ---

double deriv(const std::function<double(double)>& f, double a) {
    return derivative(f, a);
}

Eigen::VectorXd grad(const std::function<double(const Eigen::VectorXd&)>& f,
                     const Eigen::VectorXd& a) {
    Eigen::VectorXd result(a.size());
    for (Eigen::Index j = 0; j < a.size(); ++j) {
        auto f_trace = [&f, &a, j](double x) -> double {
            Eigen::VectorXd b = a;
            b[j] = x;
            return f(b);
        };
        result[j] = derivative(f_trace, a[j]);
    }
    return result;
}

// --- Bezier ---

Eigen::VectorXd evaluate_bezier(const std::vector<Eigen::VectorXd>& controls, double t) {
    int N = static_cast<int>(controls.size());
    int dim = static_cast<int>(controls[0].size());
    Eigen::VectorXd sum = Eigen::VectorXd::Zero(dim);

    std::vector<int> coefficients;
    if (N == 3) {
        coefficients = {1, 2, 1};
    } else {
        coefficients = {1, 3, 3, 1};
    }

    for (int j = 0; j < N; ++j) {
        sum += coefficients[j]
             * std::pow(1.0 - t, N - j - 1)
             * std::pow(t, j)
             * controls[j];
    }
    return sum;
}

// --- Euler's method ---

Eigen::MatrixXd eulers_method(
    const std::function<Eigen::VectorXd(double, const Eigen::VectorXd&)>& f,
    double t0, const Eigen::VectorXd& y0, double t1, int N) {

    double h = (t1 - t0) / N;
    int dim = static_cast<int>(y0.size());

    // Each row: [t, y0, y1, ...]
    Eigen::MatrixXd points(N + 1, 1 + dim);
    points(0, 0) = t0;
    points.block(0, 1, 1, dim) = y0.transpose();

    double t = t0;
    Eigen::VectorXd y = y0;

    for (int i = 0; i < N; ++i) {
        y += f(t, y) * h;
        t += h;
        points(i + 1, 0) = t;
        points.block(i + 1, 1, 1, dim) = y.transpose();
    }

    return points;
}

// --- Delta function (stub — requires ExpressionContext integration) ---

double delta_func(double t, double a) {
    // In the full implementation, this interacts with ExpressionContext's
    // break detection mechanism. For now, return 0 unless t ≈ a.
    if (std::abs(t - a) < 1e-10) {
        return 1.0;
    }
    return 0.0;
}

// --- Line intersection ---

Eigen::VectorXd line_intersection(
    const Eigen::VectorXd& p1, const Eigen::VectorXd& p2,
    const Eigen::VectorXd& q1, const Eigen::VectorXd& q2) {

    Eigen::VectorXd diff = p2 - p1;
    Eigen::VectorXd normal(2);
    normal[0] = -diff[1];
    normal[1] = diff[0];

    Eigen::VectorXd v = q2 - q1;
    double denom = normal.dot(v);

    if (std::abs(denom) < 1e-10) {
        // Lines are parallel; return center of bbox as fallback
        Diagram* diag = math_get_diagram();
        if (diag) {
            BBox bbox = diag->bbox();
            Eigen::VectorXd center(2);
            center[0] = (bbox[0] + bbox[2]) / 2.0;
            center[1] = (bbox[1] + bbox[3]) / 2.0;
            return center;
        }
        return 0.5 * (p1 + q1);
    }

    double t = normal.dot(q1 - p1) / denom;
    return q1 - t * v;
}

// --- Intersection / root finding ---

double intersect(
    const std::function<double(double)>& f,
    double seed,
    double interval_min,
    double interval_max) {

    double width = interval_max - interval_min;

    // Compute vertical bounds from the diagram's bbox when available
    double height = width;  // fallback
    double upper, lower;
    Diagram* diag = math_get_diagram();
    if (diag) {
        BBox bbox = diag->bbox();
        height = bbox[3] - bbox[1];
        upper = bbox[3] + height;
        lower = bbox[1] - height;
    } else {
        upper = 10.0 * width;
        lower = -10.0 * width;
    }
    double tolerance = 1e-6 * height;

    double x0 = seed;
    double y0 = f(x0);

    if (std::abs(y0) < tolerance) {
        return x0;
    }

    double dx = 0.002 * width;

    // Search left
    double x_left = -std::numeric_limits<double>::infinity();
    double x = x0;
    while (x >= interval_min) {
        x -= dx;
        double y;
        try {
            y = f(x);
        } catch (...) {
            break;
        }
        if (y > upper || y < lower) break;
        if (std::abs(y) < tolerance) { x_left = x; break; }
        if (y * y0 < 0) { x_left = x; break; }
    }
    if (std::isfinite(x_left) && std::abs(f(x_left) - f(x_left + dx)) > width) {
        x_left = -std::numeric_limits<double>::infinity();
    }

    // Search right
    double x_right = std::numeric_limits<double>::infinity();
    x = x0;
    while (x <= interval_max) {
        x += dx;
        double y;
        try {
            y = f(x);
        } catch (...) {
            break;
        }
        if (y > upper || y < lower) break;
        if (std::abs(y) < tolerance) { x_right = x; break; }
        if (y * y0 < 0) { x_right = x; break; }
    }
    if (std::isfinite(x_right) && std::abs(f(x_right) - f(x_right - dx)) > width) {
        x_right = std::numeric_limits<double>::infinity();
    }

    if (!std::isfinite(x_left) && !std::isfinite(x_right)) {
        return x0;  // Nothing found
    }

    double x1, x2;
    if (!std::isfinite(x_left)) {
        x2 = x_right;
        x1 = x_right - dx;
    } else if (!std::isfinite(x_right)) {
        x2 = x_left + dx;
        x1 = x_left;
    } else if (std::abs(x0 - x_right) < std::abs(x0 - x_left)) {
        x1 = x_right - dx;
        x2 = x_right;
    } else {
        x1 = x_left;
        x2 = x_left + dx;
    }

    // Bisection refinement
    for (int i = 0; i < 8; ++i) {
        double mid = (x1 + x2) / 2.0;
        if (f(mid) * f(x1) < 0) {
            x2 = mid;
        } else {
            x1 = mid;
        }
    }

    return (x1 + x2) / 2.0;
}

}  // namespace prefigure
