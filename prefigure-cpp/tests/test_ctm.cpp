#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "prefigure/ctm.hpp"

#include <cmath>

using namespace prefigure;
using Catch::Matchers::WithinAbs;

TEST_CASE("CTM identity transform", "[ctm]") {
    CTM ctm;
    Point2d p(3.0, 4.0);
    auto result = ctm.transform(p);
    REQUIRE_THAT(result[0], WithinAbs(3.0, 1e-10));
    REQUIRE_THAT(result[1], WithinAbs(4.0, 1e-10));
}

TEST_CASE("CTM translation", "[ctm]") {
    CTM ctm;
    ctm.translate(10.0, 20.0);
    Point2d p(1.0, 2.0);
    auto result = ctm.transform(p);
    REQUIRE_THAT(result[0], WithinAbs(11.0, 1e-10));
    REQUIRE_THAT(result[1], WithinAbs(22.0, 1e-10));
}

TEST_CASE("CTM scaling", "[ctm]") {
    CTM ctm;
    ctm.scale(2.0, 3.0);
    Point2d p(5.0, 7.0);
    auto result = ctm.transform(p);
    REQUIRE_THAT(result[0], WithinAbs(10.0, 1e-10));
    REQUIRE_THAT(result[1], WithinAbs(21.0, 1e-10));
}

TEST_CASE("CTM rotation 90 degrees", "[ctm]") {
    CTM ctm;
    ctm.rotate(90.0, "deg");
    Point2d p(1.0, 0.0);
    auto result = ctm.transform(p);
    REQUIRE_THAT(result[0], WithinAbs(0.0, 1e-10));
    REQUIRE_THAT(result[1], WithinAbs(1.0, 1e-10));
}

TEST_CASE("CTM push/pop", "[ctm]") {
    CTM ctm;
    ctm.translate(5.0, 5.0);
    ctm.push();
    ctm.translate(10.0, 10.0);

    Point2d p(0.0, 0.0);
    auto moved = ctm.transform(p);
    REQUIRE_THAT(moved[0], WithinAbs(15.0, 1e-10));
    REQUIRE_THAT(moved[1], WithinAbs(15.0, 1e-10));

    ctm.pop();
    auto restored = ctm.transform(p);
    REQUIRE_THAT(restored[0], WithinAbs(5.0, 1e-10));
    REQUIRE_THAT(restored[1], WithinAbs(5.0, 1e-10));
}

TEST_CASE("CTM inverse transform", "[ctm]") {
    CTM ctm;
    ctm.translate(10.0, 20.0);
    ctm.scale(2.0, 3.0);

    Point2d p(5.0, 7.0);
    auto forward = ctm.transform(p);
    auto back = ctm.inverse_transform(forward);
    REQUIRE_THAT(back[0], WithinAbs(p[0], 1e-10));
    REQUIRE_THAT(back[1], WithinAbs(p[1], 1e-10));
}

TEST_CASE("CTM composition: translate then scale", "[ctm]") {
    CTM ctm;
    ctm.translate(1.0, 1.0);
    ctm.scale(2.0, 2.0);

    Point2d p(3.0, 4.0);
    auto result = ctm.transform(p);
    // translate(1,1) then scale(2,2): (3,4) -> scale first: (6,8) -> translate: (7,9)
    // Wait, CTM concat order: ctm = ctm * new_transform
    // So translate(1,1) sets ctm = T(1,1)
    // Then scale(2,2) sets ctm = T(1,1) * S(2,2)
    // Transform p: T(1,1) * S(2,2) * p = T(1,1) * (6,8) = (7,9)
    REQUIRE_THAT(result[0], WithinAbs(7.0, 1e-10));
    REQUIRE_THAT(result[1], WithinAbs(9.0, 1e-10));
}

TEST_CASE("SVG string generators", "[ctm]") {
    REQUIRE(translatestr(10.0, 20.0) == "translate(10.0,20.0)");
    REQUIRE(scalestr(2.0, 3.0) == "scale(2,3)");
    REQUIRE(rotatestr(45.0) == "rotate(-45.0)");
}

TEST_CASE("CTM copy is independent", "[ctm]") {
    CTM ctm;
    ctm.translate(5.0, 5.0);
    CTM ctm2 = ctm.copy();
    ctm2.translate(10.0, 10.0);

    Point2d p(0.0, 0.0);
    auto r1 = ctm.transform(p);
    auto r2 = ctm2.transform(p);

    REQUIRE_THAT(r1[0], WithinAbs(5.0, 1e-10));
    REQUIRE_THAT(r2[0], WithinAbs(15.0, 1e-10));
}
