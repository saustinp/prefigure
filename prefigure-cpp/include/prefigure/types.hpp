#pragma once

#include <Eigen/Dense>
#include <pugixml.hpp>

#include <array>
#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace prefigure {

/**
 * @brief 2D point represented as an Eigen 2-vector.
 *
 * Components are accessed as p[0] (x) and p[1] (y).
 */
using Point2d = Eigen::Vector2d;

/**
 * @brief 2x3 affine transformation matrix stored as an Eigen fixed-size matrix.
 *
 * Rows correspond to output (x, y); columns to (input_x, input_y, translation).
 * The implicit third row is [0, 0, 1].
 */
using Matrix2x3 = Eigen::Matrix<double, 2, 3>;

/**
 * @brief Axis-aligned bounding box stored as [x_min, y_min, x_max, y_max].
 *
 * Coordinates are in the user (mathematical) coordinate system, not SVG pixels.
 */
using BBox = std::array<double, 4>;  // [x_min, y_min, x_max, y_max]

/**
 * @brief Thin alias for a pugixml node handle.
 *
 * XmlNode is a lightweight, non-owning handle (pointer-like semantics).
 * Copying it does not copy the underlying XML tree.
 */
using XmlNode = pugi::xml_node;

/**
 * @brief Owning XML document that holds the entire XML tree in memory.
 *
 * XmlNode handles obtained from this document are invalidated when the
 * document is destroyed.
 */
using XmlDoc = pugi::xml_document;

/**
 * @brief Output format for diagram rendering.
 */
enum class OutputFormat {
    /** @brief Standard SVG output for screen display. */
    SVG,
    /** @brief Tactile output for embossed graphics (fixed 828x792 page). */
    Tactile
};

/**
 * @brief Environment in which PreFigure is running.
 *
 * Controls ID generation strategy and annotation defaults.
 */
enum class Environment {
    /** @brief Running inside a PreTeXt build pipeline. */
    Pretext,
    /** @brief Running from the PreFigure command-line tool. */
    PfCli,
    /** @brief Running in the browser via Pyodide (playground). */
    Pyodide
};

/**
 * @brief Outline rendering status for two-pass tactile output.
 *
 * Tactile diagrams use a two-pass approach: first a wide white stroke
 * is drawn underneath (AddOutline), then the actual stroke is drawn on
 * top (FinishOutline) so that overlapping paths remain distinguishable.
 *
 * @see Diagram::add_outline()
 * @see Diagram::finish_outline()
 */
enum class OutlineStatus {
    /** @brief Normal rendering (no outline pass). */
    None,
    /** @brief First pass: create wide white background strokes. */
    AddOutline,
    /** @brief Second pass: draw the colored foreground strokes. */
    FinishOutline
};

// Forward declarations
class Diagram;
class CTM;
class ExpressionContext;

// Forward declaration for the variant
struct Value;

/**
 * @brief Single-argument mathematical function stored in the expression namespace.
 *
 * Accepts a Value (typically a scalar or vector) and returns a Value.
 * Used for user-defined functions like f(x) = x^2.
 */
using MathFunction = std::function<Value(Value)>;

/**
 * @brief Two-argument mathematical function stored in the expression namespace.
 *
 * Accepts two Values and returns a Value.  Primarily used for ODE right-hand
 * sides of the form f(t, y).
 */
using MathFunction2 = std::function<Value(Value, Value)>;

/**
 * @brief Dynamically-typed value used throughout the expression evaluator.
 *
 * A Value can hold one of the following alternatives:
 * - std::monostate -- empty / uninitialized
 * - double -- scalar number
 * - Eigen::VectorXd -- numeric array / vector (also used for 2D points)
 * - std::string -- text, color literals ("#ff0000"), or unresolved names
 * - MathFunction -- single-argument callable
 * - MathFunction2 -- two-argument callable (e.g., ODE RHS)
 * - std::vector<std::string> -- array of strings
 *
 * @details
 * The `is_*` methods test which alternative is active.
 * The `as_*` methods return a reference/copy and throw std::bad_variant_access
 * if the wrong alternative is active.
 * The `to_double` method provides a lenient conversion that also handles
 * single-element vectors.
 *
 * @note Constructing a Value from an int silently promotes to double.
 *       Constructing from a Point2d widens to Eigen::VectorXd.
 */
struct Value {
    /** @brief The variant holding the actual data. */
    std::variant<
        std::monostate,              // empty
        double,                      // scalar
        Eigen::VectorXd,             // vector/array
        std::string,                 // string (e.g., color "#ff0000")
        MathFunction,                // single-arg function
        MathFunction2,               // two-arg function (for ODEs: f(t, y))
        std::vector<std::string>,    // string array
        Eigen::MatrixXd              // 2D matrix (e.g., ODE solution)
    > data;

    /** @brief Construct an empty (monostate) value. */
    Value() : data(std::monostate{}) {}

    /** @brief Construct a scalar value. */
    Value(double d) : data(d) {}

    /** @brief Construct a vector/array value. */
    Value(const Eigen::VectorXd& v) : data(v) {}

    /** @brief Construct a string value (also used for color literals). */
    Value(const std::string& s) : data(s) {}

    /** @brief Construct a single-argument function value. */
    Value(MathFunction f) : data(std::move(f)) {}

    /** @brief Construct a two-argument function value. */
    Value(MathFunction2 f) : data(std::move(f)) {}

    /** @brief Construct a string-array value. */
    Value(const std::vector<std::string>& v) : data(v) {}

    /** @brief Construct a 2D matrix value (e.g., ODE solution). */
    Value(const Eigen::MatrixXd& m) : data(m) {}

    /**
     * @brief Convenience constructor: promotes int to double.
     * @param i Integer value to store as a double.
     */
    Value(int i) : data(static_cast<double>(i)) {}

    /**
     * @brief Convenience constructor: widens a 2D point to VectorXd.
     * @param p A 2D point.
     */
    Value(const Point2d& p) : data(Eigen::VectorXd(p)) {}

    /** @brief Return true if this value holds no data (monostate). */
    bool is_empty() const { return std::holds_alternative<std::monostate>(data); }

    /** @brief Return true if this value holds a scalar double. */
    bool is_double() const { return std::holds_alternative<double>(data); }

    /** @brief Return true if this value holds a numeric vector/array. */
    bool is_vector() const { return std::holds_alternative<Eigen::VectorXd>(data); }

    /** @brief Return true if this value holds a string. */
    bool is_string() const { return std::holds_alternative<std::string>(data); }

    /** @brief Return true if this value holds a single-argument function. */
    bool is_function() const { return std::holds_alternative<MathFunction>(data); }

    /** @brief Return true if this value holds a two-argument function. */
    bool is_function2() const { return std::holds_alternative<MathFunction2>(data); }

    /** @brief Return true if this value holds a 2D matrix. */
    bool is_matrix() const { return std::holds_alternative<Eigen::MatrixXd>(data); }

    /**
     * @brief Get the scalar double.
     * @throws std::bad_variant_access if the active alternative is not double.
     */
    double as_double() const { return std::get<double>(data); }

    /**
     * @brief Get a const reference to the numeric vector.
     * @throws std::bad_variant_access if the active alternative is not VectorXd.
     */
    const Eigen::VectorXd& as_vector() const { return std::get<Eigen::VectorXd>(data); }

    /**
     * @brief Get a const reference to the string.
     * @throws std::bad_variant_access if the active alternative is not string.
     */
    const std::string& as_string() const { return std::get<std::string>(data); }

    /**
     * @brief Get a const reference to the single-argument function.
     * @throws std::bad_variant_access if the active alternative is not MathFunction.
     */
    const MathFunction& as_function() const { return std::get<MathFunction>(data); }

    /**
     * @brief Get a const reference to the two-argument function.
     * @throws std::bad_variant_access if the active alternative is not MathFunction2.
     */
    const MathFunction2& as_function2() const { return std::get<MathFunction2>(data); }

    /**
     * @brief Get a const reference to the 2D matrix.
     * @throws std::bad_variant_access if the active alternative is not MatrixXd.
     */
    const Eigen::MatrixXd& as_matrix() const { return std::get<Eigen::MatrixXd>(data); }

    /**
     * @brief Extract this value as a 2D point.
     *
     * Assumes the underlying vector has at least two elements.
     *
     * @return A Point2d constructed from the first two vector components.
     * @throws std::bad_variant_access if not a vector.
     *
     * @note No bounds check is performed; calling this on a vector with
     *       fewer than 2 elements is undefined behavior.
     */
    Point2d as_point() const {
        const auto& v = as_vector();
        return Point2d(v[0], v[1]);
    }

    /**
     * @brief Leniently convert to double.
     *
     * If the value is a scalar, returns it directly.
     * If it is a single-element vector, returns that element.
     *
     * @return The scalar representation of this value.
     * @throws std::runtime_error if conversion is not possible.
     */
    double to_double() const {
        if (is_double()) return as_double();
        if (is_vector() && as_vector().size() == 1) return as_vector()[0];
        throw std::runtime_error("Value cannot be converted to double");
    }
};

/**
 * @brief Function signature for element handler callbacks.
 *
 * Every graphical XML element (e.g., `<line>`, `<circle>`, `<graph>`) is
 * processed by a handler matching this signature.  Handlers are registered
 * in the tag dispatcher (tags.hpp) and invoked during Diagram::parse().
 *
 * @param element  The source XML element to render.
 * @param diagram  The parent Diagram providing CTM, namespace, and SVG tree.
 * @param parent   The SVG node under which output should be appended.
 * @param status   The current outline rendering pass.
 *
 * @see tags::parse_element()
 */
using ElementHandler = std::function<void(XmlNode, Diagram&, XmlNode, OutlineStatus)>;

/**
 * @brief Convert an OutputFormat enum to its string representation.
 * @param f The output format.
 * @return "svg" or "tactile".
 */
inline std::string to_string(OutputFormat f) {
    switch (f) {
        case OutputFormat::SVG: return "svg";
        case OutputFormat::Tactile: return "tactile";
    }
    return "svg";
}

/**
 * @brief Parse an OutputFormat from a string.
 * @param s The string to parse (case-sensitive).
 * @return OutputFormat::Tactile if s == "tactile", otherwise OutputFormat::SVG.
 */
inline OutputFormat output_format_from_string(const std::string& s) {
    if (s == "tactile") return OutputFormat::Tactile;
    return OutputFormat::SVG;
}

/**
 * @brief Convert an Environment enum to its string representation.
 * @param e The environment.
 * @return "pretext", "pf_cli", or "pyodide".
 */
inline std::string to_string(Environment e) {
    switch (e) {
        case Environment::Pretext: return "pretext";
        case Environment::PfCli: return "pf_cli";
        case Environment::Pyodide: return "pyodide";
    }
    return "pretext";
}

/**
 * @brief Parse an Environment from a string.
 * @param s The string to parse (case-sensitive).
 * @return The matching Environment, defaulting to Environment::Pretext.
 */
inline Environment environment_from_string(const std::string& s) {
    if (s == "pyodide") return Environment::Pyodide;
    if (s == "pf_cli") return Environment::PfCli;
    return Environment::Pretext;
}

}  // namespace prefigure
