#pragma once

#include <Eigen/Dense>
#include <pugixml.hpp>

#include <array>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace prefigure {

// Core geometric types
using Point2d = Eigen::Vector2d;
using Matrix2x3 = Eigen::Matrix<double, 2, 3>;
using BBox = std::array<double, 4>;  // [x_min, y_min, x_max, y_max]

// XML types (thin aliases for pugixml)
using XmlNode = pugi::xml_node;
using XmlDoc = pugi::xml_document;

// Output format for diagram rendering
enum class OutputFormat {
    SVG,
    Tactile
};

// Environment in which prefigure is running
enum class Environment {
    Pretext,
    PfCli,
    Pyodide
};

// Outline rendering status for two-pass tactile output
enum class OutlineStatus {
    None,
    AddOutline,
    FinishOutline
};

// Forward declarations
class Diagram;
class CTM;
class ExpressionContext;

// Value type for the expression evaluator
// Can hold: scalar, vector, string, callable, or nothing
struct Value;

using MathFunction = std::function<Value(Value)>;
using MathFunction2 = std::function<Value(Value, Value)>;

struct Value {
    std::variant<
        std::monostate,              // empty
        double,                      // scalar
        Eigen::VectorXd,             // vector/array
        std::string,                 // string (e.g., color "#ff0000")
        MathFunction,                // single-arg function
        MathFunction2,               // two-arg function (for ODEs: f(t, y))
        std::vector<std::string>     // string array
    > data;

    Value() : data(std::monostate{}) {}
    Value(double d) : data(d) {}
    Value(const Eigen::VectorXd& v) : data(v) {}
    Value(const std::string& s) : data(s) {}
    Value(MathFunction f) : data(std::move(f)) {}
    Value(MathFunction2 f) : data(std::move(f)) {}
    Value(const std::vector<std::string>& v) : data(v) {}

    // Convenience constructors
    Value(int i) : data(static_cast<double>(i)) {}
    Value(const Point2d& p) : data(Eigen::VectorXd(p)) {}

    bool is_empty() const { return std::holds_alternative<std::monostate>(data); }
    bool is_double() const { return std::holds_alternative<double>(data); }
    bool is_vector() const { return std::holds_alternative<Eigen::VectorXd>(data); }
    bool is_string() const { return std::holds_alternative<std::string>(data); }
    bool is_function() const { return std::holds_alternative<MathFunction>(data); }
    bool is_function2() const { return std::holds_alternative<MathFunction2>(data); }

    double as_double() const { return std::get<double>(data); }
    const Eigen::VectorXd& as_vector() const { return std::get<Eigen::VectorXd>(data); }
    const std::string& as_string() const { return std::get<std::string>(data); }
    const MathFunction& as_function() const { return std::get<MathFunction>(data); }
    const MathFunction2& as_function2() const { return std::get<MathFunction2>(data); }

    // Convert to Point2d (assumes 2-element vector)
    Point2d as_point() const {
        const auto& v = as_vector();
        return Point2d(v[0], v[1]);
    }

    // Convert scalar or 1-element vector to double
    double to_double() const {
        if (is_double()) return as_double();
        if (is_vector() && as_vector().size() == 1) return as_vector()[0];
        throw std::runtime_error("Value cannot be converted to double");
    }
};

// Element handler function signature used by the tag dispatcher
using ElementHandler = std::function<void(XmlNode, Diagram&, XmlNode, OutlineStatus)>;

// String conversion helpers for OutputFormat and Environment
inline std::string to_string(OutputFormat f) {
    switch (f) {
        case OutputFormat::SVG: return "svg";
        case OutputFormat::Tactile: return "tactile";
    }
    return "svg";
}

inline OutputFormat output_format_from_string(const std::string& s) {
    if (s == "tactile") return OutputFormat::Tactile;
    return OutputFormat::SVG;
}

inline std::string to_string(Environment e) {
    switch (e) {
        case Environment::Pretext: return "pretext";
        case Environment::PfCli: return "pf_cli";
        case Environment::Pyodide: return "pyodide";
    }
    return "pretext";
}

inline Environment environment_from_string(const std::string& s) {
    if (s == "pyodide") return Environment::Pyodide;
    if (s == "pf_cli") return Environment::PfCli;
    return Environment::Pretext;
}

}  // namespace prefigure
