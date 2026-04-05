#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "prefigure/user_namespace.hpp"

#include <cmath>

using namespace prefigure;
using Catch::Matchers::WithinAbs;

TEST_CASE("ExpressionContext evaluates constants", "[user_namespace]") {
    ExpressionContext ctx;

    SECTION("pi") {
        auto v = ctx.eval("pi");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(M_PI, 1e-10));
    }

    SECTION("e") {
        auto v = ctx.eval("e");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(M_E, 1e-10));
    }
}

TEST_CASE("ExpressionContext evaluates arithmetic", "[user_namespace]") {
    ExpressionContext ctx;

    SECTION("simple addition") {
        auto v = ctx.eval("2 + 3");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(5.0, 1e-10));
    }

    SECTION("multiplication") {
        auto v = ctx.eval("4 * 5");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(20.0, 1e-10));
    }

    SECTION("power with ^") {
        auto v = ctx.eval("2^3");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(8.0, 1e-10));
    }
}

TEST_CASE("ExpressionContext evaluates trig functions", "[user_namespace]") {
    ExpressionContext ctx;

    auto v = ctx.eval("sin(pi/2)");
    REQUIRE(v.is_double());
    REQUIRE_THAT(v.as_double(), WithinAbs(1.0, 1e-10));
}

TEST_CASE("ExpressionContext handles variable definitions", "[user_namespace]") {
    ExpressionContext ctx;

    ctx.define("a = 5");
    auto v = ctx.eval("a");
    REQUIRE(v.is_double());
    REQUIRE_THAT(v.as_double(), WithinAbs(5.0, 1e-10));

    ctx.define("b = a + 3");
    auto v2 = ctx.eval("b");
    REQUIRE(v2.is_double());
    REQUIRE_THAT(v2.as_double(), WithinAbs(8.0, 1e-10));
}

TEST_CASE("ExpressionContext handles vector literals", "[user_namespace]") {
    ExpressionContext ctx;

    auto v = ctx.eval("(1, 2, 3)");
    REQUIRE(v.is_vector());
    REQUIRE(v.as_vector().size() == 3);
    REQUIRE_THAT(v.as_vector()[0], WithinAbs(1.0, 1e-10));
    REQUIRE_THAT(v.as_vector()[1], WithinAbs(2.0, 1e-10));
    REQUIRE_THAT(v.as_vector()[2], WithinAbs(3.0, 1e-10));
}

TEST_CASE("ExpressionContext handles color strings", "[user_namespace]") {
    ExpressionContext ctx;

    auto v = ctx.eval("#ff0000");
    REQUIRE(v.is_string());
    REQUIRE(v.as_string() == "#ff0000");

    auto v2 = ctx.eval("rgb(255,0,0)");
    REQUIRE(v2.is_string());
    REQUIRE(v2.as_string() == "rgb(255,0,0)");
}

TEST_CASE("ExpressionContext defines functions", "[user_namespace]") {
    ExpressionContext ctx;

    ctx.define("f(x) = x^2 + 1");
    auto f = ctx.retrieve("f");
    REQUIRE(f.is_function());

    auto result = f.as_function()(Value(3.0));
    REQUIRE(result.is_double());
    REQUIRE_THAT(result.as_double(), WithinAbs(10.0, 1e-10));
}

TEST_CASE("ExpressionContext enter_namespace and retrieve", "[user_namespace]") {
    ExpressionContext ctx;

    ctx.enter_namespace("myvar", Value(42.0));
    REQUIRE(ctx.has("myvar"));

    auto v = ctx.retrieve("myvar");
    REQUIRE(v.is_double());
    REQUIRE_THAT(v.as_double(), WithinAbs(42.0, 1e-10));
}

TEST_CASE("User-defined function calls in expressions", "[user_namespace]") {
    ExpressionContext ctx;

    SECTION("simple function call: f(3)") {
        ctx.define("f(x) = x^2");
        auto v = ctx.eval("f(3)");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(9.0, 1e-10));
    }

    SECTION("function call with arithmetic argument: f(3+1)") {
        ctx.define("f(x) = x^2");
        auto v = ctx.eval("f(3+1)");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(16.0, 1e-10));
    }

    SECTION("compound expression: f(3) + 2") {
        ctx.define("f(x) = x^2");
        auto v = ctx.eval("f(3) + 2");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(11.0, 1e-10));
    }

    SECTION("nested function calls: f(g(2))") {
        ctx.define("g(x) = x + 1");
        ctx.define("f(x) = x^2");
        auto v = ctx.eval("f(g(2))");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(9.0, 1e-10));
    }

    SECTION("two-arg function: h(2, 3)") {
        ctx.define("h(x, y) = x + y");
        auto v = ctx.eval("h(2, 3)");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(5.0, 1e-10));
    }

    SECTION("function call with variable: f(a) after a=4") {
        ctx.define("f(x) = x^2 + 1");
        ctx.define("a = 4");
        auto v = ctx.eval("f(a)");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(17.0, 1e-10));
    }

    SECTION("multiple function calls: f(2) + f(3)") {
        ctx.define("f(x) = x^2");
        auto v = ctx.eval("f(2) + f(3)");
        REQUIRE(v.is_double());
        REQUIRE_THAT(v.as_double(), WithinAbs(13.0, 1e-10));
    }
}
