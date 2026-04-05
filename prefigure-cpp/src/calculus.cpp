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

    double delta_scale = 1.0;
    for (int i = 0; i < k; ++i) {
        double delta = h / delta_scale;
        E.push_back((f(a + delta) - f(a)) / delta);
        delta_scale *= 2.0;
    }

    int j = 1;
    double pow2j = 2.0;
    while (E.size() > 1) {
        std::vector<double> next_E;
        next_E.reserve(E.size() - 1);
        for (size_t i = 0; i < E.size() - 1; ++i) {
            next_E.push_back(E[i + 1] + (E[i + 1] - E[i]) / (pow2j - 1.0));
        }
        E = std::move(next_E);
        ++j;
        pow2j *= 2.0;
    }

    return E[0];
}

}  // namespace prefigure
