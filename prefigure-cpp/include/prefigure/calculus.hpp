#pragma once

#include <functional>

namespace prefigure {

/**
 * @brief Compute the numerical derivative of f at point a.
 *
 * Uses Richardson extrapolation with an initial step size of 0.1 and 4
 * refinement iterations, yielding accuracy typically around O(h^16).
 *
 * @param f The scalar function to differentiate.
 * @param a The point at which to evaluate the derivative.
 * @return An approximation of f'(a).
 *
 * @see richardson()
 */
double derivative(const std::function<double(double)>& f, double a);

/**
 * @brief Approximate f'(a) via Richardson extrapolation.
 *
 * Computes k forward-difference approximations with successively halved
 * step sizes (h, h/2, h/4, ..., h/2^{k-1}), then applies Richardson's
 * extrapolation tableau to cancel successive error terms and produce a
 * high-order estimate.
 *
 * @param f The scalar function to differentiate.
 * @param a The point at which to evaluate the derivative.
 * @param h The initial step size.
 * @param k The number of refinement levels (iterations).
 * @return An approximation of f'(a) with error O(h^{2k}).
 *
 * @details The algorithm:
 *   1. Compute E_i = (f(a + h/2^i) - f(a)) / (h/2^i) for i = 0..k-1.
 *   2. Repeatedly combine adjacent entries:
 *      E_i^{j+1} = E_{i+1}^j + (E_{i+1}^j - E_i^j) / (2^{j+1} - 1)
 *      until a single value remains.
 */
double richardson(const std::function<double(double)>& f, double a, double h, int k);

}  // namespace prefigure
