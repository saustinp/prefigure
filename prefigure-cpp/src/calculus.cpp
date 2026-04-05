#include "prefigure/calculus.hpp"

#include <cmath>
#include <vector>

namespace prefigure {

double derivative(const std::function<double(double)>& f, double a) {
    return richardson(f, a, 0.1, 4);
}

double richardson(const std::function<double(double)>& f, double a, double h, int k) {
    std::vector<double> E;
    E.reserve(k);

    for (int i = 0; i < k; ++i) {
        double delta = h / std::pow(2.0, i);
        E.push_back((f(a + delta) - f(a)) / delta);
    }

    int j = 1;
    while (E.size() > 1) {
        std::vector<double> next_E;
        next_E.reserve(E.size() - 1);
        for (size_t i = 0; i < E.size() - 1; ++i) {
            next_E.push_back(E[i + 1] + (E[i + 1] - E[i]) / (std::pow(2.0, j) - 1.0));
        }
        E = std::move(next_E);
        ++j;
    }

    return E[0];
}

}  // namespace prefigure
