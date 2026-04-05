#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "prefigure/math_utilities.hpp"

#include <cmath>

using namespace prefigure;
using Catch::Matchers::WithinAbs;

TEST_CASE("Vector operations", "[math_utilities]") {
    Eigen::VectorXd u(2), v(2);
    u << 3.0, 4.0;
    v << 1.0, 0.0;

    SECTION("dot product") {
        REQUIRE_THAT(dot(u, v), WithinAbs(3.0, 1e-10));
    }

    SECTION("length") {
        REQUIRE_THAT(length(u), WithinAbs(5.0, 1e-10));
    }

    SECTION("distance") {
        REQUIRE_THAT(distance(u, v), WithinAbs(std::sqrt(4.0 + 16.0), 1e-10));
    }

    SECTION("normalize") {
        auto n = normalize(u);
        REQUIRE_THAT(n[0], WithinAbs(0.6, 1e-10));
        REQUIRE_THAT(n[1], WithinAbs(0.8, 1e-10));
    }

    SECTION("midpoint") {
        auto m = midpoint(u, v);
        REQUIRE_THAT(m[0], WithinAbs(2.0, 1e-10));
        REQUIRE_THAT(m[1], WithinAbs(2.0, 1e-10));
    }
}

TEST_CASE("angle function", "[math_utilities]") {
    Eigen::VectorXd p(2);
    p << 1.0, 0.0;
    REQUIRE_THAT(angle(p, "deg"), WithinAbs(0.0, 1e-10));

    Eigen::VectorXd q(2);
    q << 0.0, 1.0;
    REQUIRE_THAT(angle(q, "deg"), WithinAbs(90.0, 1e-10));
}

TEST_CASE("rotate 2D vector", "[math_utilities]") {
    Eigen::VectorXd v(2);
    v << 1.0, 0.0;
    auto rotated = rotate(v, M_PI / 2.0);
    REQUIRE_THAT(rotated[0], WithinAbs(0.0, 1e-10));
    REQUIRE_THAT(rotated[1], WithinAbs(1.0, 1e-10));
}

TEST_CASE("choose (binomial coefficient)", "[math_utilities]") {
    REQUIRE_THAT(choose(5, 2), WithinAbs(10.0, 1e-10));
    REQUIRE_THAT(choose(10, 0), WithinAbs(1.0, 1e-10));
    REQUIRE_THAT(choose(4, 4), WithinAbs(1.0, 1e-10));
}

TEST_CASE("indicator functions", "[math_utilities]") {
    REQUIRE(chi_oo(0, 1, 0.5) == 1.0);
    REQUIRE(chi_oo(0, 1, 0.0) == 0.0);
    REQUIRE(chi_oo(0, 1, 1.0) == 0.0);

    REQUIRE(chi_cc(0, 1, 0.0) == 1.0);
    REQUIRE(chi_cc(0, 1, 1.0) == 1.0);
}

TEST_CASE("Bezier evaluation", "[math_utilities]") {
    // Quadratic Bezier: P0=(0,0), P1=(1,2), P2=(2,0)
    std::vector<Eigen::VectorXd> controls(3);
    controls[0] = Eigen::VectorXd(2); controls[0] << 0.0, 0.0;
    controls[1] = Eigen::VectorXd(2); controls[1] << 1.0, 2.0;
    controls[2] = Eigen::VectorXd(2); controls[2] << 2.0, 0.0;

    auto mid = evaluate_bezier(controls, 0.5);
    REQUIRE_THAT(mid[0], WithinAbs(1.0, 1e-10));
    REQUIRE_THAT(mid[1], WithinAbs(1.0, 1e-10));
}

TEST_CASE("vec_append", "[math_utilities]") {
    Eigen::VectorXd v(3);
    v << 1.0, 2.0, 3.0;
    auto result = vec_append(v, 4.0);
    REQUIRE(result.size() == 4);
    REQUIRE_THAT(result[0], WithinAbs(1.0, 1e-10));
    REQUIRE_THAT(result[3], WithinAbs(4.0, 1e-10));
}

TEST_CASE("zip_lists", "[math_utilities]") {
    Eigen::VectorXd a(3), b(3);
    a << 1.0, 2.0, 3.0;
    b << 4.0, 5.0, 6.0;
    auto result = zip_lists(a, b);
    REQUIRE(result.size() == 3);
    REQUIRE_THAT(result[0][0], WithinAbs(1.0, 1e-10));
    REQUIRE_THAT(result[0][1], WithinAbs(4.0, 1e-10));
    REQUIRE_THAT(result[2][0], WithinAbs(3.0, 1e-10));
    REQUIRE_THAT(result[2][1], WithinAbs(6.0, 1e-10));
}

TEST_CASE("zip_lists unequal lengths", "[math_utilities]") {
    Eigen::VectorXd a(2), b(4);
    a << 1.0, 2.0;
    b << 10.0, 20.0, 30.0, 40.0;
    auto result = zip_lists(a, b);
    REQUIRE(result.size() == 2);
    REQUIRE_THAT(result[1][0], WithinAbs(2.0, 1e-10));
    REQUIRE_THAT(result[1][1], WithinAbs(20.0, 1e-10));
}

TEST_CASE("line intersection", "[math_utilities]") {
    Eigen::VectorXd p1(2), p2(2), q1(2), q2(2);
    p1 << 0.0, 0.0; p2 << 2.0, 2.0;
    q1 << 0.0, 2.0; q2 << 2.0, 0.0;

    auto result = line_intersection(p1, p2, q1, q2);
    REQUIRE_THAT(result[0], WithinAbs(1.0, 1e-10));
    REQUIRE_THAT(result[1], WithinAbs(1.0, 1e-10));
}
