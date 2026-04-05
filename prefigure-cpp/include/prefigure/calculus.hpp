#pragma once

#include <functional>

namespace prefigure {

// Compute the numerical derivative of f at point a using Richardson extrapolation.
double derivative(const std::function<double(double)>& f, double a);

// Richardson extrapolation: approximates f'(a) with step h and k iterations.
double richardson(const std::function<double(double)>& f, double a, double h, int k);

}  // namespace prefigure
