#pragma once

#include "types.hpp"

#include <Eigen/Dense>

#include <cmath>
#include <functional>
#include <vector>

namespace prefigure {

// Forward declaration
class Diagram;

/**
 * @brief Set the global Diagram pointer used by math utility functions.
 *
 * Some math functions (e.g., those requiring coordinate transforms) need
 * access to the active Diagram.  This pointer is set during Diagram construction.
 *
 * @param d Pointer to the active Diagram, or nullptr to clear.
 */
void math_set_diagram(Diagram* d);

/**
 * @brief Get the global Diagram pointer.
 * @return Pointer to the active Diagram, or nullptr if not set.
 */
Diagram* math_get_diagram();

// -- Trigonometric convenience functions ------------------------------------

/**
 * @brief Natural logarithm: ln(x) = log_e(x).
 * @param x Input value (must be positive).
 * @return The natural logarithm of x.
 */
inline double ln(double x) { return std::log(x); }

/**
 * @brief Secant: sec(x) = 1/cos(x).
 * @param x Angle in radians.
 * @return The secant of x.
 */
inline double sec(double x) { return 1.0 / std::cos(x); }

/**
 * @brief Cosecant: csc(x) = 1/sin(x).
 * @param x Angle in radians.
 * @return The cosecant of x.
 */
inline double csc(double x) { return 1.0 / std::sin(x); }

/**
 * @brief Cotangent: cot(x) = 1/tan(x) = cos(x)/sin(x).
 * @param x Angle in radians.
 * @return The cotangent of x.
 */
inline double cot(double x) { return 1.0 / std::tan(x); }

// -- Vector operations ------------------------------------------------------

/**
 * @brief Compute the dot product of two vectors: u . v = sum(u_i * v_i).
 * @param u First vector.
 * @param v Second vector (must have the same dimension as u).
 * @return The scalar dot product.
 */
double dot(const Eigen::VectorXd& u, const Eigen::VectorXd& v);

/**
 * @brief Compute the Euclidean distance between two points: ||p - q||.
 * @param p First point.
 * @param q Second point (must have the same dimension as p).
 * @return The L2 distance.
 */
double distance(const Eigen::VectorXd& p, const Eigen::VectorXd& q);

/**
 * @brief Compute the Euclidean length (L2 norm) of a vector: ||u||.
 * @param u The input vector.
 * @return The length of u.
 */
double length(const Eigen::VectorXd& u);

/**
 * @brief Normalize a vector to unit length: u / ||u||.
 * @param u The input vector.
 * @return A unit vector in the same direction as u.
 * @throws std::runtime_error if u has length less than 1e-15.
 */
Eigen::VectorXd normalize(const Eigen::VectorXd& u);

/**
 * @brief Compute the midpoint of two vectors: (u + v) / 2.
 * @param u First point/vector.
 * @param v Second point/vector.
 * @return The component-wise average.
 */
Eigen::VectorXd midpoint(const Eigen::VectorXd& u, const Eigen::VectorXd& v);

/**
 * @brief Compute the angle of a 2D vector measured from the positive x-axis.
 *
 * Uses atan2(y, x) to compute the angle in [-180, 180] degrees or [-pi, pi] radians.
 *
 * @param p A 2D vector (at least 2 elements).
 * @param units "deg" (default) for degrees, anything else for radians.
 * @return The angle of the vector.
 */
double angle(const Eigen::VectorXd& p, const std::string& units = "deg");

/**
 * @brief Rotate a 2D vector counter-clockwise by theta radians.
 *
 * Applies the standard 2D rotation matrix:
 *   [cos(theta) -sin(theta)] [v[0]]
 *   [sin(theta)  cos(theta)] [v[1]]
 *
 * @param v     A 2D vector.
 * @param theta Rotation angle in radians.
 * @return The rotated 2D vector.
 */
Eigen::VectorXd rotate(const Eigen::VectorXd& v, double theta);

/**
 * @brief Circularly shift array elements by 1 position (numpy-style roll).
 *
 * The last element becomes the first; all others shift right by one index.
 *
 * @param array The input vector.
 * @return A new vector with elements rolled by 1.
 */
Eigen::VectorXd roll(const Eigen::VectorXd& array);

// -- Combinatorics ----------------------------------------------------------

/**
 * @brief Compute the binomial coefficient C(n, k) = n! / (k! * (n-k)!).
 *
 * Uses the multiplicative formula for numerical stability.
 * Arguments are truncated to integers.
 *
 * @param n Total items (truncated to int).
 * @param k Items to choose (truncated to int).
 * @return The binomial coefficient, or 0 if k < 0 or k > n.
 */
double choose(double n, double k);

// -- Indicator (characteristic) functions -----------------------------------

/**
 * @brief Characteristic function of the open interval (a, b).
 * @param a Left endpoint (excluded).
 * @param b Right endpoint (excluded).
 * @param t Test value.
 * @return 1.0 if a < t < b, otherwise 0.0.
 */
double chi_oo(double a, double b, double t);

/**
 * @brief Characteristic function of the half-open interval (a, b].
 * @param a Left endpoint (excluded).
 * @param b Right endpoint (included).
 * @param t Test value.
 * @return 1.0 if a < t <= b, otherwise 0.0.
 */
double chi_oc(double a, double b, double t);

/**
 * @brief Characteristic function of the half-open interval [a, b).
 * @param a Left endpoint (included).
 * @param b Right endpoint (excluded).
 * @param t Test value.
 * @return 1.0 if a <= t < b, otherwise 0.0.
 */
double chi_co(double a, double b, double t);

/**
 * @brief Characteristic function of the closed interval [a, b].
 * @param a Left endpoint (included).
 * @param b Right endpoint (included).
 * @param t Test value.
 * @return 1.0 if a <= t <= b, otherwise 0.0.
 */
double chi_cc(double a, double b, double t);

// -- Numerical calculus -----------------------------------------------------

/**
 * @brief Compute the numerical derivative of f at a.
 *
 * Thin wrapper that delegates to calculus::derivative().
 *
 * @param f The scalar function to differentiate.
 * @param a The evaluation point.
 * @return An approximation of f'(a).
 *
 * @see derivative()
 */
double deriv(const std::function<double(double)>& f, double a);

/**
 * @brief Compute the numerical gradient of a multivariate scalar function.
 *
 * Computes partial derivatives along each coordinate axis using
 * Richardson extrapolation.
 *
 * @param f A scalar function of a vector argument.
 * @param a The evaluation point.
 * @return A vector of partial derivatives [df/dx_0, df/dx_1, ...].
 *
 * @see derivative()
 */
Eigen::VectorXd grad(const std::function<double(const Eigen::VectorXd&)>& f,
                     const Eigen::VectorXd& a);

// -- Bezier evaluation ------------------------------------------------------

/**
 * @brief Evaluate a Bezier curve at parameter t.
 *
 * Supports quadratic (3 control points) and cubic (4 control points)
 * Bezier curves using the explicit Bernstein polynomial formula:
 *   B(t) = sum_{j=0}^{N-1} C_j * (1-t)^{N-1-j} * t^j * P_j
 *
 * @param controls Vector of control points (each is an Eigen::VectorXd).
 * @param t        Parameter value in [0, 1].
 * @return The point on the curve at parameter t.
 *
 * @note Only 3 or 4 control points are supported.
 */
Eigen::VectorXd evaluate_bezier(const std::vector<Eigen::VectorXd>& controls, double t);

// -- ODE solvers ------------------------------------------------------------

/**
 * @brief Solve an ODE initial value problem using Euler's method.
 *
 * Integrates dy/dt = f(t, y) from t0 to t1 with N uniform steps.
 *
 * @param f  The ODE right-hand side: dy/dt = f(t, y).
 * @param t0 Initial time.
 * @param y0 Initial state vector.
 * @param t1 Final time.
 * @param N  Number of time steps.
 * @return An (N+1) x (1 + dim) matrix where each row is [t, y_0, y_1, ...].
 */
Eigen::MatrixXd eulers_method(
    const std::function<Eigen::VectorXd(double, const Eigen::VectorXd&)>& f,
    double t0, const Eigen::VectorXd& y0, double t1, int N);

// -- Discontinuity handling -------------------------------------------------

/**
 * @brief Dirac delta function approximation for ODE discontinuities.
 *
 * Returns 1.0 when |t - a| < 1e-10, otherwise 0.0.
 * In the full implementation, this interacts with ExpressionContext's
 * break-detection mechanism.
 *
 * @param t Current time.
 * @param a Location of the impulse.
 * @return 1.0 if t is approximately equal to a, otherwise 0.0.
 */
double delta_func(double t, double a);

// -- Geometry ---------------------------------------------------------------

/**
 * @brief Find the intersection point of two infinite lines.
 *
 * Each line is defined by two points.  Uses the normal-based formula:
 * compute the normal to line 1, then find where line 2 crosses it.
 *
 * @param p1 First point on line 1.
 * @param p2 Second point on line 1.
 * @param q1 First point on line 2.
 * @param q2 Second point on line 2.
 * @return The intersection point, or the midpoint of p1 and q1 if the lines are parallel.
 *
 * @note Returns a fallback midpoint (not an error) when lines are parallel
 *       (denominator < 1e-10).
 */
Eigen::VectorXd line_intersection(
    const Eigen::VectorXd& p1, const Eigen::VectorXd& p2,
    const Eigen::VectorXd& q1, const Eigen::VectorXd& q2);

/**
 * @brief Find a root of f near a seed point using search and bisection.
 *
 * Searches outward from @p seed in both directions within
 * [interval_min, interval_max] to find a sign change, then refines
 * with 8 bisection iterations.
 *
 * @param f            The scalar function whose root is sought.
 * @param seed         Starting point for the search.
 * @param interval_min Left bound of the search interval.
 * @param interval_max Right bound of the search interval.
 * @return An approximate root of f, or @p seed if no sign change is found.
 *
 * @details The search step size is 0.002 * (interval_max - interval_min).
 *          Values that exceed 10x the interval width are treated as divergent.
 */
double intersect(
    const std::function<double(double)>& f,
    double seed,
    double interval_min,
    double interval_max);

}  // namespace prefigure
