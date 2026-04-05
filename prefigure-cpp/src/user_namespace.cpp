#include "prefigure/user_namespace.hpp"
#include "prefigure/calculus.hpp"
#include "prefigure/math_utilities.hpp"

#include <spdlog/spdlog.h>

// exprtk is a massive header-only library; include it here only
#include <exprtk.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace prefigure {

// ============================================================================
// Internal exprtk wrapper
// ============================================================================

// We maintain a single exprtk symbol_table and parser that can be reused.
// Variables are bound by reference through a map of named doubles.

struct ExprtkState {
    exprtk::symbol_table<double> symbol_table;
    exprtk::parser<double> parser;

    // Named scalar variables that exprtk binds by reference
    std::unordered_map<std::string, double> scalar_vars;

    ExprtkState() {
        symbol_table.add_constants();  // pi, epsilon, inf
    }

    // Ensure a scalar variable exists and return reference to it
    double& ensure_var(const std::string& name) {
        auto it = scalar_vars.find(name);
        if (it == scalar_vars.end()) {
            scalar_vars[name] = 0.0;
            symbol_table.add_variable(name, scalar_vars[name]);
        }
        return scalar_vars[name];
    }

    // Set a scalar variable value
    void set_var(const std::string& name, double value) {
        ensure_var(name) = value;
    }

    // Compile and evaluate a scalar expression
    double eval(const std::string& expr_str) {
        exprtk::expression<double> expression;
        expression.register_symbol_table(symbol_table);
        if (!parser.compile(expr_str, expression)) {
            throw std::runtime_error(
                "Expression compilation failed: " + expr_str +
                " -- " + parser.error());
        }
        return expression.value();
    }
};

// ============================================================================
// ExpressionContext implementation
// ============================================================================

ExpressionContext::ExpressionContext() {
    init_builtins();
}

ExpressionContext::~ExpressionContext() = default;

void ExpressionContext::init_builtins() {
    // Register mathematical constants
    enter_namespace("e", Value(M_E));
    enter_namespace("pi", Value(M_PI));
    enter_namespace("inf", Value(std::numeric_limits<double>::infinity()));

    variables_.insert({"e", "pi", "inf"});

    // Register all built-in function names for validation
    // (The actual implementations are dispatched in eval())
    const std::vector<std::string> builtin_funcs = {
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2",
        "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
        "exp", "log", "log2", "log10", "sqrt", "cbrt",
        "abs", "fabs", "ceil", "floor", "round",
        "pow", "fmod", "max", "min",
        // From math_utilities
        "ln", "sec", "csc", "cot",
        "dot", "distance", "length", "normalize", "midpoint", "angle",
        "rotate", "roll", "choose",
        "chi_oo", "chi_oc", "chi_co", "chi_cc",
        "deriv", "grad",
        "evaluate_bezier", "eulers_method",
        "intersect", "line_intersection",
        "delta",
    };
    for (const auto& f : builtin_funcs) {
        functions_.insert(f);
    }
}

std::string ExpressionContext::preprocess(const std::string& expr, bool substitution) const {
    std::string result = expr;

    // Strip leading/trailing whitespace
    auto ltrim = result.find_first_not_of(" \t\n\r");
    if (ltrim != std::string::npos) result = result.substr(ltrim);
    auto rtrim = result.find_last_not_of(" \t\n\r");
    if (rtrim != std::string::npos) result = result.substr(0, rtrim + 1);

    if (substitution) {
        // Replace ^ with ** (exprtk uses ^ for power natively, so we keep it)
        // Actually, exprtk already supports ^ as power, so just keep it.
        // But Python code replaces ^ with **, and exprtk treats ^ as power.
        // No transformation needed for exprtk.
    }

    return result;
}

Value ExpressionContext::eval(const std::string& raw_expr,
                             const std::optional<std::string>& name,
                             bool substitution) {
    if (raw_expr.empty()) {
        spdlog::error("Evaluating an empty expression");
        throw std::runtime_error("Evaluating an empty expression");
    }

    std::string expr = preprocess(raw_expr, substitution);

    // Check if it's a color string
    if (expr[0] == '#') {
        Value result(expr);
        if (name) {
            enter_namespace(*name, result);
        }
        return result;
    }
    if (expr.substr(0, 3) == "rgb") {
        Value result(expr);
        if (name) {
            enter_namespace(*name, result);
        }
        return result;
    }

    // Check for assignment/function definition: contains '='
    auto eq_pos = expr.find('=');
    if (eq_pos != std::string::npos && eq_pos > 0 && expr[eq_pos - 1] != '!'
        && expr[eq_pos - 1] != '<' && expr[eq_pos - 1] != '>') {
        // Check it's not ==
        if (eq_pos + 1 < expr.size() && expr[eq_pos + 1] == '=') {
            // It's a comparison, not assignment — fall through
        } else {
            std::string lhs = expr.substr(0, eq_pos);
            std::string rhs = expr.substr(eq_pos + 1);

            // Trim
            while (!lhs.empty() && lhs.back() == ' ') lhs.pop_back();
            while (!rhs.empty() && rhs.front() == ' ') rhs.erase(rhs.begin());

            // Is it a function definition? (has parentheses in lhs)
            auto open = lhs.find('(');
            auto close = lhs.find(')');
            if (open != std::string::npos && close != std::string::npos) {
                std::string func_name = lhs.substr(0, open);
                while (!func_name.empty() && func_name.back() == ' ') func_name.pop_back();
                std::string args_str = lhs.substr(open + 1, close - open - 1);

                // Parse argument names
                std::vector<std::string> arg_names;
                std::istringstream arg_stream(args_str);
                std::string arg;
                while (std::getline(arg_stream, arg, ',')) {
                    auto start = arg.find_first_not_of(" ");
                    auto end = arg.find_last_not_of(" ");
                    if (start != std::string::npos) {
                        arg_names.push_back(arg.substr(start, end - start + 1));
                    }
                }

                // Store as a lambda that evaluates the rhs with bound arguments
                // For now, we create a simple evaluating closure
                std::string body = rhs;
                if (substitution) {
                    // ^ is already power in exprtk, no change needed
                }

                if (arg_names.size() == 1) {
                    // Single argument function
                    auto captured_body = body;
                    auto captured_arg = arg_names[0];
                    auto* ctx = this;

                    MathFunction func = [ctx, captured_body, captured_arg](Value arg_val) -> Value {
                        // Create a temporary scope
                        Value old;
                        bool had_old = ctx->has(captured_arg);
                        if (had_old) old = ctx->retrieve(captured_arg);

                        ctx->enter_namespace(captured_arg, arg_val);
                        Value result = ctx->eval(captured_body, std::nullopt, false);

                        if (had_old) ctx->enter_namespace(captured_arg, old);
                        else ctx->namespace_.erase(captured_arg);

                        return result;
                    };

                    functions_.insert(func_name);
                    variables_.insert(func_name);
                    namespace_[func_name] = Value(func);
                    return Value(func);

                } else if (arg_names.size() == 2) {
                    // Two argument function (e.g., for ODEs: f(t, y))
                    auto captured_body = body;
                    auto captured_args = arg_names;
                    auto* ctx = this;

                    MathFunction2 func = [ctx, captured_body, captured_args](Value a1, Value a2) -> Value {
                        Value old1, old2;
                        bool had1 = ctx->has(captured_args[0]);
                        bool had2 = ctx->has(captured_args[1]);
                        if (had1) old1 = ctx->retrieve(captured_args[0]);
                        if (had2) old2 = ctx->retrieve(captured_args[1]);

                        ctx->enter_namespace(captured_args[0], a1);
                        ctx->enter_namespace(captured_args[1], a2);
                        Value result = ctx->eval(captured_body, std::nullopt, false);

                        if (had1) ctx->enter_namespace(captured_args[0], old1);
                        else ctx->namespace_.erase(captured_args[0]);
                        if (had2) ctx->enter_namespace(captured_args[1], old2);
                        else ctx->namespace_.erase(captured_args[1]);

                        return result;
                    };

                    functions_.insert(func_name);
                    variables_.insert(func_name);
                    namespace_[func_name] = Value(func);
                    return Value(func);
                }
            } else {
                // Simple assignment: name = expression
                Value result = eval(rhs, std::nullopt, substitution);
                std::string var_name = lhs;
                variables_.insert(var_name);
                namespace_[var_name] = result;
                if (result.is_double()) {
                    // Also register in exprtk if we have one
                }
                return result;
            }
        }
    }

    // Try to evaluate as a tuple/vector literal: (a, b, c) or [a, b, c]
    auto vec_result = try_eval_vector(expr);
    if (vec_result) {
        Value result(*vec_result);
        if (name) {
            enter_namespace(*name, result);
        }
        return result;
    }

    // Try to look up as a namespace variable
    auto it = namespace_.find(expr);
    if (it != namespace_.end()) {
        Value result = it->second;
        if (name) {
            enter_namespace(*name, result);
        }
        return result;
    }

    // Try scalar evaluation with exprtk
    // First, we need to set up exprtk with current namespace scalars
    try {
        exprtk::symbol_table<double> sym;
        sym.add_constants();

        // Add all scalar variables from namespace
        std::unordered_map<std::string, double> var_storage;
        for (const auto& [n, v] : namespace_) {
            if (v.is_double()) {
                var_storage[n] = v.as_double();
            }
        }
        for (auto& [n, v] : var_storage) {
            sym.add_variable(n, v);
        }

        exprtk::expression<double> expression;
        expression.register_symbol_table(sym);

        exprtk::parser<double> parser;
        if (parser.compile(expr, expression)) {
            double result_val = expression.value();
            Value result(result_val);
            if (name) {
                enter_namespace(*name, result);
            }
            return result;
        }
    } catch (...) {
        // Fall through to string return
    }

    // If nothing works, treat it as a plain string
    Value result(expr);
    if (name) {
        enter_namespace(*name, result);
    }
    return result;
}

void ExpressionContext::define(const std::string& expression, bool substitution) {
    auto eq_pos = expression.find('=');
    if (eq_pos == std::string::npos) {
        spdlog::error("Unrecognized definition: {}", expression);
        return;
    }

    std::string left = expression.substr(0, eq_pos);
    std::string right = expression.substr(eq_pos + 1);
    while (!left.empty() && left.back() == ' ') left.pop_back();
    while (!right.empty() && right.front() == ' ') right.erase(right.begin());

    if (left.find('(') != std::string::npos) {
        // Function definition
        eval(expression, std::nullopt, substitution);
    } else {
        // Variable definition
        eval(right, left, substitution);
    }
}

void ExpressionContext::enter_namespace(const std::string& name, const Value& value) {
    namespace_[name] = value;
    variables_.insert(name);
}

void ExpressionContext::enter_function(const std::string& name, MathFunction func) {
    namespace_[name] = Value(std::move(func));
    functions_.insert(name);
    variables_.insert(name);
}

Value ExpressionContext::retrieve(const std::string& name) const {
    auto it = namespace_.find(name);
    if (it != namespace_.end()) return it->second;
    throw std::runtime_error("Name not found in namespace: " + name);
}

bool ExpressionContext::has(const std::string& name) const {
    return namespace_.find(name) != namespace_.end();
}

void ExpressionContext::register_derivative(const std::string& f_name, const std::string& df_name) {
    auto f_val = retrieve(f_name);
    if (!f_val.is_function()) {
        spdlog::error("Cannot take derivative of non-function: {}", f_name);
        return;
    }
    auto f = f_val.as_function();
    MathFunction df = [f](Value x) -> Value {
        double xd = x.to_double();
        auto f_scalar = [&f](double t) -> double {
            return f(Value(t)).to_double();
        };
        return Value(derivative(f_scalar, xd));
    };
    enter_function(df_name, std::move(df));
}

// --- Vector literal parsing ---

std::optional<Eigen::VectorXd> ExpressionContext::try_eval_vector(const std::string& expr) const {
    if (expr.empty()) return std::nullopt;

    char first = expr.front();
    char last = expr.back();

    bool is_tuple = (first == '(' && last == ')');
    bool is_list = (first == '[' && last == ']');

    if (!is_tuple && !is_list) return std::nullopt;

    // Extract the inner content
    std::string inner = expr.substr(1, expr.size() - 2);

    // Split by commas, respecting nested parentheses/brackets
    std::vector<std::string> elements;
    int depth = 0;
    std::string current;
    for (char c : inner) {
        if (c == '(' || c == '[') ++depth;
        if (c == ')' || c == ']') --depth;
        if (c == ',' && depth == 0) {
            elements.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) elements.push_back(current);

    if (elements.size() < 2) return std::nullopt;  // Single element tuples not treated as vectors

    // Evaluate each element
    Eigen::VectorXd result(static_cast<Eigen::Index>(elements.size()));
    for (size_t i = 0; i < elements.size(); ++i) {
        try {
            // Recursive eval — but we need a non-const version
            // For now, try exprtk scalar eval
            auto* self = const_cast<ExpressionContext*>(this);
            Value v = self->eval(elements[i], std::nullopt, false);
            result[static_cast<Eigen::Index>(i)] = v.to_double();
        } catch (...) {
            return std::nullopt;  // If any element fails, it's not a vector
        }
    }

    return result;
}

// --- ODE break/delta detection ---

void ExpressionContext::initialize_breaks() {
    static std::vector<double> breaks_storage;
    breaks_storage.clear();
    breaks_ = &breaks_storage;
}

std::vector<double> ExpressionContext::find_breaks(const MathFunction2& f, double t, const Value& y) {
    initialize_breaks();
    f(Value(t), y);
    auto result = breaks_ ? *breaks_ : std::vector<double>{};
    finish_breaks();
    return result;
}

Value ExpressionContext::measure_de_jump(const MathFunction2& f, double t, const Value& y) {
    delta_on_ = true;
    Value f1 = f(Value(t), y);
    delta_on_ = false;
    Value f0 = f(Value(t), y);

    // Return f1 - f0
    if (f1.is_double() && f0.is_double()) {
        return Value(f1.as_double() - f0.as_double());
    }
    if (f1.is_vector() && f0.is_vector()) {
        return Value(Eigen::VectorXd(f1.as_vector() - f0.as_vector()));
    }
    return Value(0.0);
}

void ExpressionContext::finish_breaks() {
    breaks_ = nullptr;
}

}  // namespace prefigure
