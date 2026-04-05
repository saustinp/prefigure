#pragma once

#include "types.hpp"

#include <Eigen/Dense>

#include <cmath>
#include <functional>
#include <vector>

namespace prefigure {

// Forward declaration — the Diagram pointer is set by the diagram module
class Diagram;
void math_set_diagram(Diagram* d);
Diagram* math_get_diagram();

// Trigonometric convenience functions
inline double ln(double x) { return std::log(x); }
inline double sec(double x) { return 1.0 / std::cos(x); }
inline double csc(double x) { return 1.0 / std::sin(x); }
inline double cot(double x) { return 1.0 / std::tan(x); }

// Vector operations
double dot(const Eigen::VectorXd& u, const Eigen::VectorXd& v);
double distance(const Eigen::VectorXd& p, const Eigen::VectorXd& q);
double length(const Eigen::VectorXd& u);
Eigen::VectorXd normalize(const Eigen::VectorXd& u);
Eigen::VectorXd midpoint(const Eigen::VectorXd& u, const Eigen::VectorXd& v);

// Angle of a 2D vector in degrees (default) or radians
double angle(const Eigen::VectorXd& p, const std::string& units = "deg");

// Rotate a 2D vector by theta radians
Eigen::VectorXd rotate(const Eigen::VectorXd& v, double theta);

// Array roll (numpy-style, shift by 1 along axis 0)
Eigen::VectorXd roll(const Eigen::VectorXd& array);

// Combinatorics
double choose(double n, double k);

// Indicator (characteristic) functions for intervals
double chi_oo(double a, double b, double t);  // open-open
double chi_oc(double a, double b, double t);  // open-closed
double chi_co(double a, double b, double t);  // closed-open
double chi_cc(double a, double b, double t);  // closed-closed

// Numerical derivative (delegates to calculus module)
double deriv(const std::function<double(double)>& f, double a);

// Numerical gradient of a multivariate function
Eigen::VectorXd grad(const std::function<double(const Eigen::VectorXd&)>& f,
                     const Eigen::VectorXd& a);

// Bezier curve evaluation (quadratic or cubic based on control point count)
Eigen::VectorXd evaluate_bezier(const std::vector<Eigen::VectorXd>& controls, double t);

// Euler's method for ODE initial value problems
// Returns matrix where each row is [t, y0, y1, ...]
Eigen::MatrixXd eulers_method(
    const std::function<Eigen::VectorXd(double, const Eigen::VectorXd&)>& f,
    double t0, const Eigen::VectorXd& y0, double t1, int N);

// Dirac delta function for ODE discontinuities
double delta_func(double t, double a);

// Line intersection: given two lines each defined by two points,
// returns the intersection point
Eigen::VectorXd line_intersection(
    const Eigen::VectorXd& p1, const Eigen::VectorXd& p2,
    const Eigen::VectorXd& q1, const Eigen::VectorXd& q2);

// Find intersection of two functions or zero of a function via bisection
double intersect(
    const std::function<double(double)>& f,
    double seed,
    double interval_min,
    double interval_max);

}  // namespace prefigure
