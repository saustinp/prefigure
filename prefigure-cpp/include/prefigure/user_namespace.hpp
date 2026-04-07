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

// Forward declaration for the cached exprtk parser/symbol_table state.  The
// definition lives in user_namespace.cpp so this header doesn't need to drag
// in the ~16k-line exprtk.hpp.
struct ExprtkState;

// Forward declaration of the per-function compiled state holder.  Definition
// is private to user_namespace.cpp.
struct CompiledScalar2State;

/**
 * @brief A raw-function-pointer view onto a 2-argument scalar user function.
 *
 * The general MathFunction2 API uses std::function<Value(Value, Value)>,
 * which costs one indirect call (via the function-handler vtable), one or
 * two heap-allocated callable bodies, and Value-variant copy/destroy on
 * every invocation.  For the inner-loop callsites in implicit.cpp /
 * slope_field.cpp / diffeqs.cpp, that overhead was large enough to be
 * visible in the perf profile.
 *
 * CompiledFunction2 is the lighter alternative.  It's a 16-byte POD that
 * holds a raw function pointer (`invoke_`) and an opaque data pointer
 * (`impl_`).  Calling it costs exactly one indirect call -- the same as
 * any plain `double(*)(double, double)` function pointer would.  All the
 * inner machinery is hidden inside the static dispatcher function whose
 * address is stored in `invoke_`.
 *
 * Obtain one with ExpressionContext::get_compiled_scalar2().  The returned
 * object is only valid as long as the ExpressionContext (and the underlying
 * compiled AST it wraps) is alive -- which in practice means "for the
 * duration of the current diagram parse," and is guaranteed by Diagram's
 * lifetime.
 */
struct CompiledFunction2 {
    using InvokeFn = double(*)(const void*, double, double);

    const void* impl_ = nullptr;
    InvokeFn invoke_ = nullptr;

    bool valid() const noexcept { return invoke_ != nullptr; }

    // Single-instruction-call hot-path entry point: just one indirect
    // call to invoke_, which is a static function so the compiler can
    // inline through it once profile-guided optimisation kicks in.
    inline double operator()(double x, double y) const {
        return invoke_(impl_, x, y);
    }
};

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

    /**
     * @brief Look up a 2-arg scalar user function by name and return a raw
     *        function-pointer view of it for hot inner loops.
     *
     * `name_or_expr` is typically the literal value of the XML `function="..."`
     * attribute, e.g. `"f"`.  We accept whitespace around the name.  If the
     * expression is anything more complex (e.g. an inline expression or a
     * function-of-function), this returns std::nullopt and the caller falls
     * back to the regular MathFunction2 path.
     *
     * The function must have been defined via a 2-arg `f(x,y) = ...`
     * definition AND must have been eligible for the scalar fast path
     * (is_scalar_only true at definition time).  If either condition fails,
     * std::nullopt is returned.
     *
     * @return CompiledFunction2 wrapping a raw function pointer, or
     *         std::nullopt if the function isn't a registered scalar
     *         2-arg user function.
     */
    std::optional<CompiledFunction2>
    get_compiled_scalar2(const std::string& name_or_expr) const;

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

    // Cached exprtk parser + symbol_table.  Constructing the parser is
    // expensive (it builds large internal operator/identifier tables); the
    // pre-Phase-1.5 code constructed a fresh parser on every label/attribute
    // eval, which showed up at ~3% of total runtime in the implicit profile.
    // Created lazily on first eval() call.  Held by unique_ptr so we don't
    // need to include exprtk.hpp from this header.
    std::unique_ptr<ExprtkState> exprtk_state_;

    // Per-function compiled state for the scalar 2-arg fast path used by
    // CompiledFunction2.  Each entry holds the AST shared pointer plus a
    // back-reference to this context, so the dispatcher can find them via
    // a single pointer-cast.  Stable addresses are required (the
    // CompiledFunction2 holds a raw pointer into the value), so we use
    // unique_ptr<...> as the value type rather than the bare struct.
    std::unordered_map<std::string, std::unique_ptr<CompiledScalar2State>>
        compiled_scalar2_;

    // Pre-process expression: handle ^ substitution, detect tuples
    std::string preprocess(const std::string& expr, bool substitution) const;

    // Replace array subscript patterns like name[index] with their scalar values
    std::string replace_array_subscripts(const std::string& expr);

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
