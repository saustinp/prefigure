#pragma once

#include "types.hpp"

#include <Eigen/Dense>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace prefigure {

/**
 * @brief Safe evaluation context for mathematical expressions authored in XML.
 *
 * ExpressionContext replaces the Python implementation's global user_namespace
 * module.  It provides expression parsing and evaluation using exprtk as the
 * backend, with custom preprocessing for:
 * - Tuple/list literals: `(1, 2, 3)` and `[1, 2, 3]` become Eigen::VectorXd
 * - Function definitions: `f(x) = x^2` becomes a stored MathFunction
 * - Two-argument functions: `f(t, y) = t*y` becomes a MathFunction2 (for ODEs)
 * - Color literals: `#ff0000` and `rgb(...)` are returned as string Values
 * - Variable assignments: `a = 3` stores a named scalar
 *
 * @par Key differences from the Python implementation
 * - Instance-based (no global state) -- created fresh per Diagram
 * - Uses exprtk instead of Python's AST + eval()
 * - Tuple/list literals are pre-parsed and converted to Eigen::VectorXd
 * - Function definitions are compiled once and stored as std::function objects
 *
 * @note Non-copyable because it owns internal exprtk compilation state.
 *
 * @see Diagram::expr_ctx() for obtaining the context associated with a diagram.
 * @see Value for the dynamically-typed result of evaluation.
 */
class ExpressionContext {
public:
    /**
     * @brief Construct a new expression context with built-in math functions and constants.
     *
     * Registers constants (pi, e, inf) and all built-in function names
     * (sin, cos, ln, dot, distance, etc.) for validation.
     */
    ExpressionContext();

    /** @brief Destructor. */
    ~ExpressionContext();

    // Non-copyable (owns exprtk state)
    ExpressionContext(const ExpressionContext&) = delete;
    ExpressionContext& operator=(const ExpressionContext&) = delete;

    /**
     * @brief Evaluate an expression string and return the result.
     *
     * The evaluation pipeline tries these strategies in order:
     * 1. Color literal (`#hex` or `rgb(...)`) -- returned as a string Value
     * 2. Assignment or function definition (contains `=`)
     * 3. Tuple/vector literal (`(a, b)` or `[a, b]`)
     * 4. Namespace variable lookup
     * 5. Scalar expression via exprtk
     * 6. Fallback: treat as a plain string
     *
     * @param expr         The expression string to evaluate.
     * @param name         If provided, the result is also stored in the namespace under this name.
     * @param substitution If true (default), apply preprocessing (currently a no-op for exprtk
     *                     since `^` is natively supported as power).
     *
     * @return The evaluated Value.
     *
     * @throws std::runtime_error if the expression is empty.
     *
     * @note If evaluation fails at all stages, the raw expression string is returned as a
     *       string Value rather than throwing.
     */
    Value eval(const std::string& expr,
               const std::optional<std::string>& name = std::nullopt,
               bool substitution = true);

    /**
     * @brief Parse and execute a definition string.
     *
     * Splits on `=` to determine whether this is a variable assignment
     * (`a = expr`) or a function definition (`f(x) = expr`), then delegates
     * to eval().
     *
     * @param expression   The definition string (must contain `=`).
     * @param substitution If true, apply preprocessing.
     *
     * @note Logs an error and returns if no `=` is found.
     */
    void define(const std::string& expression, bool substitution = true);

    /**
     * @brief Enter a named value into the namespace.
     *
     * @param name  The variable name.
     * @param value The value to associate with @p name.
     */
    void enter_namespace(const std::string& name, const Value& value);

    /**
     * @brief Enter a single-argument callable function into the namespace.
     *
     * @param name The function name.
     * @param func The function implementation.
     */
    void enter_function(const std::string& name, MathFunction func);

    /**
     * @brief Retrieve a value by name from the namespace.
     *
     * @param name The variable or function name.
     * @return The stored Value.
     * @throws std::runtime_error if @p name is not found.
     */
    Value retrieve(const std::string& name) const;

    /**
     * @brief Check if a name exists in the namespace.
     * @param name The name to look up.
     * @return True if the name is present.
     */
    bool has(const std::string& name) const;

    /**
     * @brief Register a numerical derivative function.
     *
     * Given a function `f_name` already in the namespace, creates a new
     * function `df_name` that computes df/dx using Richardson extrapolation.
     *
     * @param f_name  The name of the existing function to differentiate.
     * @param df_name The name under which to store the derivative function.
     *
     * @note Logs an error if @p f_name does not refer to a MathFunction.
     *
     * @see calculus::derivative()
     */
    void register_derivative(const std::string& f_name, const std::string& df_name);

    // -- ODE break/delta detection ------------------------------------------

    /**
     * @brief Initialize break-detection state for ODE solving.
     *
     * Allocates internal storage to collect discontinuity locations
     * encountered during a trial evaluation of an ODE right-hand side.
     */
    void initialize_breaks();

    /**
     * @brief Find discontinuity locations in an ODE right-hand side function.
     *
     * Evaluates `f(t, y)` once with break detection enabled, collects any
     * break points, then cleans up.
     *
     * @param f The ODE right-hand side function f(t, y).
     * @param t The current time value.
     * @param y The current state value.
     * @return A vector of break-point locations (possibly empty).
     */
    std::vector<double> find_breaks(const MathFunction2& f, double t, const Value& y);

    /**
     * @brief Measure the jump in an ODE right-hand side at a discontinuity.
     *
     * Evaluates f(t, y) with and without the delta function active,
     * and returns the difference.
     *
     * @param f The ODE right-hand side function f(t, y).
     * @param t The time at the discontinuity.
     * @param y The state at the discontinuity.
     * @return The jump value (f_with_delta - f_without_delta).
     */
    Value measure_de_jump(const MathFunction2& f, double t, const Value& y);

    /**
     * @brief Clean up break-detection state after ODE break analysis.
     */
    void finish_breaks();

private:
    // The namespace: maps names to values
    std::unordered_map<std::string, Value> namespace_;

    // Set of known function names (for validation)
    std::unordered_set<std::string> functions_;

    // Set of known variable names (for validation)
    std::unordered_set<std::string> variables_;

    // Break detection state for ODE solving
    std::vector<double> breaks_storage_;
    std::vector<double>* breaks_ = nullptr;
    bool delta_on_ = false;

    // Pre-process expression: handle ^ substitution, detect tuples
    std::string preprocess(const std::string& expr, bool substitution) const;

    // Replace user-defined function calls in an expression with their
    // evaluated numeric results so that exprtk can handle the rest.
    std::string replace_function_calls(const std::string& expr);

    // Evaluate a scalar expression using exprtk
    double eval_scalar(const std::string& expr) const;

    // Try to evaluate as a vector/tuple literal like (1, 2, 3) or [1, 2, 3]
    std::optional<Eigen::VectorXd> try_eval_vector(const std::string& expr) const;

    // Try to evaluate as a function definition like "f(x) = x^2"
    // Returns true if it was a function definition
    bool try_define_function(const std::string& expr);

    // Initialize built-in math functions and constants
    void init_builtins();
};

}  // namespace prefigure
