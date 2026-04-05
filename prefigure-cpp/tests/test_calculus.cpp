#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "prefigure/calculus.hpp"

#include <cmath>

using namespace prefigure;
using Catch::Matchers::WithinAbs;

TEST_CASE("Richardson extrapolation computes derivatives", "[calculus]") {
    SECTION("Derivative of x^2 at x=3 is 6") {
        auto f = [](double x) { return x * x; };
        double d = derivative(f, 3.0);
        REQUIRE_THAT(d, WithinAbs(6.0, 1e-8));
    }

    SECTION("Derivative of sin(x) at x=0 is 1") {
        auto f = [](double x) { return std::sin(x); };
        double d = derivative(f, 0.0);
        REQUIRE_THAT(d, WithinAbs(1.0, 1e-7));
    }

    SECTION("Derivative of exp(x) at x=1 is e") {
        auto f = [](double x) { return std::exp(x); };
        double d = derivative(f, 1.0);
        REQUIRE_THAT(d, WithinAbs(M_E, 1e-6));
    }

    SECTION("Derivative of x^3 at x=2 is 12") {
        auto f = [](double x) { return x * x * x; };
        double d = derivative(f, 2.0);
        REQUIRE_THAT(d, WithinAbs(12.0, 1e-6));
    }
}
