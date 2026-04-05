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

/// ExpressionContext replaces Python's user_namespace module.
/// It provides safe evaluation of mathematical expressions authored in XML
/// using exprtk as the backend parser/evaluator, with custom pre-processing
/// for tuple/list literals, array indexing, and function definitions.
///
/// Key differences from Python implementation:
/// - Instance-based (no global state) — created fresh per diagram
/// - Uses exprtk instead of Python AST + eval()
/// - Tuple/list literals are pre-parsed and converted to Eigen::VectorXd
/// - Function definitions compiled and stored as std::function objects
class ExpressionContext {
public:
    ExpressionContext();
    ~ExpressionContext();

    // Non-copyable (owns exprtk state)
    ExpressionContext(const ExpressionContext&) = delete;
    ExpressionContext& operator=(const ExpressionContext&) = delete;

    /// Evaluate an expression string and return the result.
    /// If `name` is provided, store the result in the namespace under that name.
    /// If `substitution` is true (default), replace '^' with power operator.
    /// Handles: scalar expressions, function definitions (f(x) = expr),
    /// tuple/list literals, color strings (#hex, rgb(...)).
    Value eval(const std::string& expr,
               const std::optional<std::string>& name = std::nullopt,
               bool substitution = true);

    /// Define a variable or function from a string like "f(x) = x^2" or "a = 3"
    void define(const std::string& expression, bool substitution = true);

    /// Enter a named value into the namespace
    void enter_namespace(const std::string& name, const Value& value);

    /// Enter a callable function (one argument) into the namespace
    void enter_function(const std::string& name, MathFunction func);

    /// Retrieve a value by name
    Value retrieve(const std::string& name) const;

    /// Check if a name exists in the namespace
    bool has(const std::string& name) const;

    /// Register a numerical derivative: df_name(x) = d/dx f_name(x)
    void register_derivative(const std::string& f_name, const std::string& df_name);

    // --- ODE break/delta detection ---
    void initialize_breaks();
    std::vector<double> find_breaks(const MathFunction2& f, double t, const Value& y);
    Value measure_de_jump(const MathFunction2& f, double t, const Value& y);
    void finish_breaks();

private:
    // The namespace: maps names to values
    std::unordered_map<std::string, Value> namespace_;

    // Set of known function names (for validation)
    std::unordered_set<std::string> functions_;

    // Set of known variable names (for validation)
    std::unordered_set<std::string> variables_;

    // Break detection state for ODE solving
    std::vector<double>* breaks_ = nullptr;
    bool delta_on_ = false;

    // Pre-process expression: handle ^ substitution, detect tuples
    std::string preprocess(const std::string& expr, bool substitution) const;

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
