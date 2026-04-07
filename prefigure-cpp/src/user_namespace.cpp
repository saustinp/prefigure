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
// Recursive-descent expression evaluator
// ============================================================================
//
// Handles vectors as first-class Values throughout the expression tree.
// This replaces the older string-substitution + exprtk-only path for any
// expression that involves vector arithmetic, vector-returning functions,
// string arrays, or bare comma-separated tuples (which Python's ast.parse
// accepts at top level but exprtk does not).
//
// Grammar (informal):
//   top_expr   = expr (',' expr)*           // bare tuples become vector Values
//   expr       = term (('+'|'-') term)*
//   term       = factor (('*'|'/') factor)*
//   factor     = unary ('^' factor)?        // right-associative power
//   unary      = ('+'|'-') unary | primary
//   primary    = number
//              | string_literal             // 'foo' or "foo"
//              | '(' expr (',' expr)* ')'   // parenthesised expr or tuple
//              | '[' (item (',' item)*)? ']'// list literal (numbers or strings)
//              | identifier ('(' args ')')? // variable or function call
//              | identifier '[' expr ']'    // subscript: vec[i] or strs[i]
//
// Operator semantics extend numpy-like rules to Value:
//   scalar  op scalar  -> scalar
//   scalar  op vector  -> vector (broadcast)
//   vector  op scalar  -> vector (broadcast)
//   vector  op vector  -> vector (component-wise)
//
// Built-in functions are dispatched directly inside call_function() rather
// than being stored in the namespace, so they are always available without
// polluting the user-visible namespace.
namespace {

class RDEval {
public:
    RDEval(const std::string& src, ExpressionContext& ctx)
        : src_(src), pos_(0), ctx_(ctx) {}

    Value parse_top() {
        skip_ws();
        if (pos_ >= src_.size()) {
            throw std::runtime_error("empty expression");
        }
        Value first = parse_expr();
        skip_ws();
        // Bare top-level tuple: a, b, c
        if (pos_ < src_.size() && src_[pos_] == ',') {
            std::vector<Value> elems = {first};
            while (pos_ < src_.size() && src_[pos_] == ',') {
                ++pos_;
                elems.push_back(parse_expr());
                skip_ws();
            }
            return make_vector(elems);
        }
        if (pos_ != src_.size()) {
            throw std::runtime_error("unexpected trailing input at position " +
                                     std::to_string(pos_));
        }
        return first;
    }

private:
    const std::string& src_;
    size_t pos_;
    ExpressionContext& ctx_;

    // -- token helpers ------------------------------------------------------
    void skip_ws() {
        while (pos_ < src_.size() &&
               std::isspace(static_cast<unsigned char>(src_[pos_])))
            ++pos_;
    }
    char peek(size_t off = 0) const {
        return (pos_ + off < src_.size()) ? src_[pos_ + off] : '\0';
    }
    bool consume_char(char c) {
        skip_ws();
        if (peek() == c) { ++pos_; return true; }
        return false;
    }
    bool consume_str(const char* s) {
        skip_ws();
        size_t n = std::strlen(s);
        if (pos_ + n <= src_.size() && src_.compare(pos_, n, s) == 0) {
            pos_ += n;
            return true;
        }
        return false;
    }

    // -- grammar ------------------------------------------------------------
    Value parse_expr() {
        Value left = parse_term();
        while (true) {
            skip_ws();
            char c = peek();
            if (c == '+') { ++pos_; left = vadd(left, parse_term()); }
            else if (c == '-') { ++pos_; left = vsub(left, parse_term()); }
            else break;
        }
        return left;
    }

    Value parse_term() {
        Value left = parse_unary();
        while (true) {
            skip_ws();
            // Avoid consuming ** as two separate * tokens
            if (peek() == '*' && peek(1) == '*') break;
            char c = peek();
            if (c == '*') { ++pos_; left = vmul(left, parse_unary()); }
            else if (c == '/') { ++pos_; left = vdiv(left, parse_unary()); }
            else break;
        }
        return left;
    }

    Value parse_unary() {
        skip_ws();
        if (consume_char('+')) return parse_unary();
        if (consume_char('-')) return vneg(parse_unary());
        return parse_power();
    }

    Value parse_power() {
        Value base = parse_primary();
        skip_ws();
        if (consume_str("**") || consume_char('^')) {
            Value exp = parse_unary();   // right-associative
            return vpow(base, exp);
        }
        return base;
    }

    Value parse_primary() {
        skip_ws();
        if (pos_ >= src_.size()) {
            throw std::runtime_error("unexpected end of expression");
        }
        char c = peek();

        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && std::isdigit(static_cast<unsigned char>(peek(1))))) {
            return parse_number();
        }
        if (c == '\'' || c == '"') {
            return parse_string_literal();
        }
        if (c == '(') {
            ++pos_;
            std::vector<Value> elems;
            elems.push_back(parse_expr());
            skip_ws();
            while (consume_char(',')) {
                elems.push_back(parse_expr());
                skip_ws();
            }
            if (!consume_char(')')) {
                throw std::runtime_error("expected ')'");
            }
            if (elems.size() == 1) return elems[0];
            return make_vector(elems);
        }
        if (c == '[') {
            ++pos_;
            std::vector<Value> elems;
            skip_ws();
            if (peek() != ']') {
                elems.push_back(parse_list_item());
                skip_ws();
                while (consume_char(',')) {
                    elems.push_back(parse_list_item());
                    skip_ws();
                }
            }
            if (!consume_char(']')) {
                throw std::runtime_error("expected ']'");
            }
            return make_list(elems);
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            return parse_ident_or_call();
        }
        throw std::runtime_error(std::string("unexpected character: '") + c + "'");
    }

    Value parse_list_item() {
        skip_ws();
        if (peek() == '\'' || peek() == '"') return parse_string_literal();
        return parse_expr();
    }

    Value parse_number() {
        size_t start = pos_;
        while (pos_ < src_.size() &&
               (std::isdigit(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '.'))
            ++pos_;
        if (pos_ < src_.size() && (src_[pos_] == 'e' || src_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < src_.size() && (src_[pos_] == '+' || src_[pos_] == '-'))
                ++pos_;
            while (pos_ < src_.size() &&
                   std::isdigit(static_cast<unsigned char>(src_[pos_])))
                ++pos_;
        }
        return Value(std::stod(src_.substr(start, pos_ - start)));
    }

    Value parse_string_literal() {
        // Match Python's string-literal escape rules.  Python only honours
        // a fixed set of escape sequences (\n \t \r \\ \' \" \0 \a \b \f \v
        // and the closing quote); for any other backslash sequence such as
        // `\omega`, Python KEEPS THE BACKSLASH so the string remains 6 chars.
        // The previous implementation unconditionally dropped every backslash,
        // which silently corrupted LaTeX strings — `'\omega'` got passed to
        // MathJax as the literal text "omega", which MathJax then rendered as
        // five italic letters with invisible-times glyphs between them
        // (instead of the single ω character).  See
        // <https://docs.python.org/3/reference/lexical_analysis.html#strings>.
        char quote = src_[pos_++];
        std::string s;
        while (pos_ < src_.size() && src_[pos_] != quote) {
            if (src_[pos_] == '\\' && pos_ + 1 < src_.size()) {
                char next = src_[pos_ + 1];
                switch (next) {
                    case 'n':  s += '\n'; pos_ += 2; break;
                    case 't':  s += '\t'; pos_ += 2; break;
                    case 'r':  s += '\r'; pos_ += 2; break;
                    case '\\': s += '\\'; pos_ += 2; break;
                    case '\'': s += '\''; pos_ += 2; break;
                    case '"':  s += '"';  pos_ += 2; break;
                    case '0':  s += '\0'; pos_ += 2; break;
                    case 'a':  s += '\a'; pos_ += 2; break;
                    case 'b':  s += '\b'; pos_ += 2; break;
                    case 'f':  s += '\f'; pos_ += 2; break;
                    case 'v':  s += '\v'; pos_ += 2; break;
                    default:
                        // Unrecognised escape: emit BOTH the backslash and the
                        // following character (Python's behaviour for `\o`,
                        // `\w`, etc.).  Critical for LaTeX strings inside
                        // <definition> blocks.
                        s += '\\';
                        s += next;
                        pos_ += 2;
                        break;
                }
            } else {
                s += src_[pos_++];
            }
        }
        if (pos_ < src_.size()) ++pos_;  // skip closing quote
        return Value(s);
    }

    Value parse_ident_or_call() {
        size_t start = pos_;
        while (pos_ < src_.size() &&
               (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_'))
            ++pos_;
        std::string name = src_.substr(start, pos_ - start);

        skip_ws();
        if (peek() == '(') {
            ++pos_;
            std::vector<Value> args;
            skip_ws();
            if (peek() != ')') {
                args.push_back(parse_expr());
                skip_ws();
                while (consume_char(',')) {
                    args.push_back(parse_expr());
                    skip_ws();
                }
            }
            if (!consume_char(')')) {
                throw std::runtime_error("expected ')' in call to " + name);
            }
            return call_function(name, args);
        }
        if (peek() == '[') {
            ++pos_;
            Value idx = parse_expr();
            skip_ws();
            if (!consume_char(']')) {
                throw std::runtime_error("expected ']' in subscript " + name);
            }
            return subscript(name, idx);
        }
        return lookup_ident(name);
    }

    // -- value construction -------------------------------------------------
    static Value make_vector(const std::vector<Value>& elems) {
        // If every element is a scalar, build an Eigen::VectorXd.
        bool all_scalar = true;
        for (const auto& e : elems) {
            if (!e.is_double() && !(e.is_vector() && e.as_vector().size() == 1)) {
                all_scalar = false;
                break;
            }
        }
        if (all_scalar) {
            Eigen::VectorXd v(static_cast<Eigen::Index>(elems.size()));
            for (size_t i = 0; i < elems.size(); ++i) {
                v[static_cast<Eigen::Index>(i)] =
                    elems[i].is_double() ? elems[i].as_double() : elems[i].as_vector()[0];
            }
            return Value(v);
        }
        // Otherwise: matrix of vectors? For our use cases, nested vectors
        // (like a list of points) are stored as Eigen::MatrixXd with rows = elements.
        if (elems[0].is_vector()) {
            Eigen::Index rows = static_cast<Eigen::Index>(elems.size());
            Eigen::Index cols = elems[0].as_vector().size();
            Eigen::MatrixXd m(rows, cols);
            for (Eigen::Index i = 0; i < rows; ++i) {
                if (!elems[i].is_vector() || elems[i].as_vector().size() != cols) {
                    throw std::runtime_error("inhomogeneous tuple cannot be combined");
                }
                m.row(i) = elems[i].as_vector().transpose();
            }
            return Value(m);
        }
        throw std::runtime_error("cannot construct vector from heterogeneous elements");
    }

    static Value make_list(const std::vector<Value>& elems) {
        // String list?
        bool all_string = !elems.empty();
        for (const auto& e : elems) {
            if (!e.is_string()) { all_string = false; break; }
        }
        if (all_string) {
            std::vector<std::string> v;
            v.reserve(elems.size());
            for (const auto& e : elems) v.push_back(e.as_string());
            return Value(v);
        }
        // Otherwise treat like a tuple
        return make_vector(elems);
    }

    // -- variable / subscript -----------------------------------------------
    Value lookup_ident(const std::string& name) {
        // Built-in constants are also looked up via the namespace.
        if (ctx_.has(name)) return ctx_.retrieve(name);
        // Some math constants might not be in the namespace; provide them here.
        if (name == "pi") return Value(M_PI);
        if (name == "e") return Value(M_E);
        if (name == "inf") return Value(std::numeric_limits<double>::infinity());
        throw std::runtime_error("unknown identifier: " + name);
    }

    Value subscript(const std::string& name, const Value& idx) {
        if (!ctx_.has(name)) {
            throw std::runtime_error("unknown identifier in subscript: " + name);
        }
        Value var = ctx_.retrieve(name);
        int i = static_cast<int>(idx.to_double());
        if (var.is_vector()) {
            const auto& v = var.as_vector();
            if (i < 0 || i >= v.size())
                throw std::runtime_error("vector index out of range");
            return Value(v[i]);
        }
        if (std::holds_alternative<std::vector<std::string>>(var.data)) {
            const auto& v = std::get<std::vector<std::string>>(var.data);
            if (i < 0 || i >= static_cast<int>(v.size()))
                throw std::runtime_error("string-array index out of range");
            return Value(v[i]);
        }
        if (var.is_matrix()) {
            const auto& m = var.as_matrix();
            if (i < 0 || i >= m.rows())
                throw std::runtime_error("matrix row index out of range");
            return Value(Eigen::VectorXd(m.row(i)));
        }
        throw std::runtime_error("cannot subscript " + name);
    }

    // -- function dispatch --------------------------------------------------
    Value call_function(const std::string& name, const std::vector<Value>& args) {
        // First: user-defined function from namespace?
        if (ctx_.has(name)) {
            Value v = ctx_.retrieve(name);
            if (v.is_function()) {
                if (args.size() != 1)
                    throw std::runtime_error("function " + name + " expects 1 argument");
                return v.as_function()(args[0]);
            }
            if (v.is_function2()) {
                if (args.size() != 2)
                    throw std::runtime_error("function " + name + " expects 2 arguments");
                return v.as_function2()(args[0], args[1]);
            }
            // Not a function value -- fall through to built-ins
        }

        auto a0 = [&](size_t i) -> double { return args.at(i).to_double(); };
        auto vec = [&](size_t i) -> Eigen::VectorXd {
            const Value& v = args.at(i);
            if (v.is_vector()) return v.as_vector();
            if (v.is_double()) {
                Eigen::VectorXd r(1); r[0] = v.as_double(); return r;
            }
            throw std::runtime_error(name + ": expected numeric argument");
        };

        // Single-argument scalar functions
        if (args.size() == 1 && args[0].is_double()) {
            double x = args[0].as_double();
            if (name == "sin")   return Value(std::sin(x));
            if (name == "cos")   return Value(std::cos(x));
            if (name == "tan")   return Value(std::tan(x));
            if (name == "asin")  return Value(std::asin(x));
            if (name == "acos")  return Value(std::acos(x));
            if (name == "atan")  return Value(std::atan(x));
            if (name == "sinh")  return Value(std::sinh(x));
            if (name == "cosh")  return Value(std::cosh(x));
            if (name == "tanh")  return Value(std::tanh(x));
            if (name == "exp")   return Value(std::exp(x));
            if (name == "log")   return Value(std::log(x));
            if (name == "ln")    return Value(std::log(x));
            if (name == "log2")  return Value(std::log2(x));
            if (name == "log10") return Value(std::log10(x));
            if (name == "sqrt")  return Value(std::sqrt(x));
            if (name == "cbrt")  return Value(std::cbrt(x));
            if (name == "abs")   return Value(std::fabs(x));
            if (name == "fabs")  return Value(std::fabs(x));
            if (name == "ceil")  return Value(std::ceil(x));
            if (name == "floor") return Value(std::floor(x));
            if (name == "round") return Value(std::round(x));
            if (name == "sec")   return Value(prefigure::sec(x));
            if (name == "csc")   return Value(prefigure::csc(x));
            if (name == "cot")   return Value(prefigure::cot(x));
        }

        // Single-argument vector functions
        if (args.size() == 1 && args[0].is_vector()) {
            const auto& v = args[0].as_vector();
            if (name == "length")    return Value(prefigure::length(v));
            if (name == "normalize") return Value(prefigure::normalize(v));
            if (name == "angle")     return Value(prefigure::angle(v, "deg"));
            if (name == "roll")      return Value(prefigure::roll(v));
        }

        // Two-argument functions
        if (args.size() == 2) {
            // Vector-vector
            if (args[0].is_vector() && args[1].is_vector()) {
                if (name == "dot")      return Value(prefigure::dot(args[0].as_vector(), args[1].as_vector()));
                if (name == "distance") return Value(prefigure::distance(args[0].as_vector(), args[1].as_vector()));
                if (name == "midpoint") return Value(prefigure::midpoint(args[0].as_vector(), args[1].as_vector()));
            }
            // Vector + scalar (rotate, angle with units)
            if (args[0].is_vector() && args[1].is_double() && name == "rotate") {
                return Value(prefigure::rotate(args[0].as_vector(), args[1].as_double()));
            }
            if (args[0].is_vector() && args[1].is_string() && name == "angle") {
                return Value(prefigure::angle(args[0].as_vector(), args[1].as_string()));
            }
            // Scalar + scalar
            if (args[0].is_double() && args[1].is_double()) {
                double a = args[0].as_double(), b = args[1].as_double();
                if (name == "atan2")  return Value(std::atan2(a, b));
                if (name == "pow")    return Value(std::pow(a, b));
                if (name == "fmod")   return Value(std::fmod(a, b));
                if (name == "max")    return Value(std::max(a, b));
                if (name == "min")    return Value(std::min(a, b));
                if (name == "choose") return Value(prefigure::choose(a, b));
            }
        }

        // Three-argument indicator functions
        if (args.size() == 3 && args[0].is_double() && args[1].is_double() && args[2].is_double()) {
            double a = args[0].as_double(), b = args[1].as_double(), t = args[2].as_double();
            if (name == "chi_oo") return Value(prefigure::chi_oo(a, b, t));
            if (name == "chi_oc") return Value(prefigure::chi_oc(a, b, t));
            if (name == "chi_co") return Value(prefigure::chi_co(a, b, t));
            if (name == "chi_cc") return Value(prefigure::chi_cc(a, b, t));
        }

        // max/min with mixed argument count -- fall back to scalar conversion
        if (name == "max" && args.size() >= 2) {
            double r = a0(0);
            for (size_t i = 1; i < args.size(); ++i) r = std::max(r, a0(i));
            return Value(r);
        }
        if (name == "min" && args.size() >= 2) {
            double r = a0(0);
            for (size_t i = 1; i < args.size(); ++i) r = std::min(r, a0(i));
            return Value(r);
        }

        throw std::runtime_error("unknown or unsupported function call: " + name +
                                 " with " + std::to_string(args.size()) + " arg(s)");
    }

    // -- Value arithmetic ---------------------------------------------------
    static Value vneg(const Value& a) {
        if (a.is_double()) return Value(-a.as_double());
        if (a.is_vector()) return Value(Eigen::VectorXd(-a.as_vector()));
        throw std::runtime_error("unary minus: unsupported operand");
    }
    static Value vadd(const Value& a, const Value& b) { return binop(a, b, '+'); }
    static Value vsub(const Value& a, const Value& b) { return binop(a, b, '-'); }
    static Value vmul(const Value& a, const Value& b) { return binop(a, b, '*'); }
    static Value vdiv(const Value& a, const Value& b) { return binop(a, b, '/'); }
    static Value vpow(const Value& a, const Value& b) {
        if (a.is_double() && b.is_double())
            return Value(std::pow(a.as_double(), b.as_double()));
        throw std::runtime_error("power requires scalar operands");
    }

    static Value binop(const Value& a, const Value& b, char op) {
        auto apply = [op](double x, double y) -> double {
            switch (op) {
                case '+': return x + y;
                case '-': return x - y;
                case '*': return x * y;
                case '/': return x / y;
            }
            return 0.0;
        };
        if (a.is_double() && b.is_double()) {
            return Value(apply(a.as_double(), b.as_double()));
        }
        if (a.is_vector() && b.is_double()) {
            Eigen::VectorXd r = a.as_vector();
            for (Eigen::Index i = 0; i < r.size(); ++i) r[i] = apply(r[i], b.as_double());
            return Value(r);
        }
        if (a.is_double() && b.is_vector()) {
            Eigen::VectorXd r = b.as_vector();
            for (Eigen::Index i = 0; i < r.size(); ++i) r[i] = apply(a.as_double(), r[i]);
            return Value(r);
        }
        if (a.is_vector() && b.is_vector()) {
            const auto& av = a.as_vector();
            const auto& bv = b.as_vector();
            if (av.size() != bv.size())
                throw std::runtime_error("vector dimensions don't match");
            Eigen::VectorXd r(av.size());
            for (Eigen::Index i = 0; i < r.size(); ++i) r[i] = apply(av[i], bv[i]);
            return Value(r);
        }
        throw std::runtime_error("binop: unsupported operand types");
    }
};

}  // anonymous namespace

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

    // Replace ** with ^ for exprtk (Python uses **, exprtk uses ^)
    {
        size_t pos = 0;
        while ((pos = result.find("**", pos)) != std::string::npos) {
            result.replace(pos, 2, "^");
            pos += 1;  // advance past the ^
        }
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

    // Check for assignment/function definition: find first non-comparison '='
    // Skip '==', '!=', '<=', '>=' by searching for '=' that isn't part of these
    size_t eq_pos = std::string::npos;
    for (size_t i = 0; i < expr.size(); ++i) {
        if (expr[i] == '=') {
            // Check it's not ==, !=, <=, >=
            bool is_comparison = false;
            if (i > 0 && (expr[i-1] == '!' || expr[i-1] == '<' || expr[i-1] == '>'))
                is_comparison = true;
            if (i + 1 < expr.size() && expr[i+1] == '=')
                is_comparison = true;
            if (!is_comparison) {
                eq_pos = i;
                break;
            }
            // Skip the second '=' of '=='
            if (i + 1 < expr.size() && expr[i+1] == '=') ++i;
        }
    }
    if (eq_pos != std::string::npos && eq_pos > 0) {
        {
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

    // -- Primary path: recursive-descent evaluator that handles vectors,
    // tuples, string arrays, and built-in vector functions natively. The
    // legacy string-substitution + exprtk pipeline below remains as a
    // fallback for any expression the RD evaluator can't parse.
    try {
        RDEval rd(expr, *this);
        Value result = rd.parse_top();
        if (name) {
            enter_namespace(*name, result);
        }
        return result;
    } catch (const std::exception& rd_err) {
        spdlog::debug("RD evaluator failed on '{}': {} -- falling back", expr, rd_err.what());
        // fall through to legacy path
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

    // Replace array subscript patterns like name[index] with scalar values
    std::string subscript_replaced = replace_array_subscripts(expr);

    // Try to evaluate user-defined function calls within the expression.
    // This pre-processes the expression string, replacing any calls to
    // known user-defined functions with their evaluated numeric results,
    // so that exprtk can handle the rest.
    std::string processed_expr = replace_function_calls(subscript_replaced);

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
        if (parser.compile(processed_expr, expression)) {
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

// --- User-defined function call evaluation ---

// Split a string by commas at parenthesis depth 0
static std::vector<std::string> split_args(const std::string& s) {
    std::vector<std::string> args;
    int depth = 0;
    std::string current;
    for (char c : s) {
        if (c == '(' || c == '[') ++depth;
        if (c == ')' || c == ']') --depth;
        if (c == ',' && depth == 0) {
            args.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) args.push_back(current);
    return args;
}

// Trim whitespace from both ends
static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

// Check if a character is valid in an identifier
static bool is_ident_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

std::string ExpressionContext::replace_array_subscripts(const std::string& expr) {
    std::string result = expr;
    std::regex sub_re(R"(([a-zA-Z_]\w*)\[([^\]]+)\])");
    std::smatch match;
    std::string working = result;

    // Iteratively replace subscript patterns from left to right
    while (std::regex_search(working, match, sub_re)) {
        std::string id_name = match[1].str();
        std::string index_expr = match[2].str();

        // Check if the identifier is a known vector in the namespace
        auto ns_it = namespace_.find(id_name);
        if (ns_it != namespace_.end() && ns_it->second.is_vector()) {
            try {
                // Evaluate the index expression
                int idx = static_cast<int>(const_cast<ExpressionContext*>(this)->eval(index_expr, std::nullopt, false).to_double());
                auto& vec = ns_it->second.as_vector();
                if (idx >= 0 && idx < static_cast<int>(vec.size())) {
                    double val = vec[idx];
                    std::string val_str = std::to_string(val);
                    // Replace the match in working
                    std::string prefix = match.prefix().str();
                    std::string suffix = match.suffix().str();
                    working = prefix + val_str + suffix;
                    continue;  // re-scan from beginning
                }
            } catch (...) {
                // If index evaluation fails, leave as-is
            }
        }
        // Not a vector subscript or failed; skip past this match
        // To avoid infinite loop, break out of matching
        break;
    }
    return working;
}

std::string ExpressionContext::replace_function_calls(const std::string& expr) {
    // Scan the expression for patterns like identifier(...) where identifier
    // is a user-defined function in namespace_. Replace each such call with
    // its evaluated numeric result so exprtk can handle the surrounding math.

    std::string result;
    size_t i = 0;
    while (i < expr.size()) {
        // Try to match an identifier starting at position i
        if (std::isalpha(static_cast<unsigned char>(expr[i])) || expr[i] == '_') {
            size_t id_start = i;
            while (i < expr.size() && is_ident_char(expr[i])) ++i;
            std::string id_name = expr.substr(id_start, i - id_start);

            // Check if followed by '(' and if id_name is a user-defined function
            if (i < expr.size() && expr[i] == '(') {
                auto ns_it = namespace_.find(id_name);
                if (ns_it != namespace_.end() && (ns_it->second.is_function() || ns_it->second.is_function2())) {
                    // Find the matching closing parenthesis
                    size_t paren_start = i;
                    int depth = 1;
                    size_t j = i + 1;
                    while (j < expr.size() && depth > 0) {
                        if (expr[j] == '(') ++depth;
                        if (expr[j] == ')') --depth;
                        ++j;
                    }
                    if (depth != 0) {
                        // Unmatched parenthesis; just pass through
                        result += id_name;
                        continue;
                    }
                    // Extract the arguments string (between the parens)
                    std::string args_str = expr.substr(paren_start + 1, j - paren_start - 2);

                    auto* self = this;

                    try {
                        auto format_value = [&](const Value& call_result, size_t pos, size_t end_pos) {
                            if (call_result.is_double()) {
                                result += std::to_string(call_result.as_double());
                            } else if (call_result.is_vector()) {
                                result += "(";
                                for (Eigen::Index vi = 0; vi < call_result.as_vector().size(); ++vi) {
                                    if (vi > 0) result += ",";
                                    result += std::to_string(call_result.as_vector()[vi]);
                                }
                                result += ")";
                            } else if (call_result.is_string()) {
                                result += call_result.as_string();
                            } else {
                                result += expr.substr(pos, end_pos - pos + 1);
                            }
                        };

                        if (ns_it->second.is_function()) {
                            auto func_args = split_args(args_str);
                            if (func_args.size() == 1) {
                                Value arg_val = self->eval(trim(func_args[0]), std::nullopt, false);
                                Value call_result = ns_it->second.as_function()(arg_val);
                                format_value(call_result, id_start, j - 1);
                            } else {
                                // Single-arg function called with wrong arg count; pass through
                                result += id_name;
                                continue;
                            }
                        } else if (ns_it->second.is_function2()) {
                            auto func_args = split_args(args_str);
                            if (func_args.size() == 2) {
                                Value arg1 = self->eval(trim(func_args[0]), std::nullopt, false);
                                Value arg2 = self->eval(trim(func_args[1]), std::nullopt, false);
                                Value call_result = ns_it->second.as_function2()(arg1, arg2);
                                format_value(call_result, id_start, j - 1);
                            } else {
                                result += id_name;
                                continue;
                            }
                        }
                        i = j;  // Advance past the closing paren
                        continue;
                    } catch (...) {
                        // If evaluation fails, pass through the original text
                        result += id_name;
                        continue;
                    }
                }
            }
            // Not a user-defined function call; just append the identifier
            result += id_name;
        } else {
            result += expr[i];
            ++i;
        }
    }
    return result;
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
    breaks_storage_.clear();
    breaks_ = &breaks_storage_;
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
