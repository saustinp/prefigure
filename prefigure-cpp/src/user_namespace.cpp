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

public:
    // -- value construction -------------------------------------------------
    // The static helpers below (make_vector, make_list, lookup_ident_impl,
    // subscript_impl, call_function_impl, vneg/vadd/.../binop) are exposed
    // publicly so the free function evaluate_ast() and the RDParser-built
    // AST can reuse them without duplicating ~250 lines of value-arithmetic
    // and dispatch code.  RDEval lives in an anonymous namespace so this
    // doesn't affect any external API.
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
        return lookup_ident_impl(name, ctx_);
    }

    static Value lookup_ident_impl(const std::string& name, ExpressionContext& ctx) {
        // Built-in constants are also looked up via the namespace.
        if (ctx.has(name)) return ctx.retrieve(name);
        // Some math constants might not be in the namespace; provide them here.
        if (name == "pi") return Value(M_PI);
        if (name == "e") return Value(M_E);
        if (name == "inf") return Value(std::numeric_limits<double>::infinity());
        throw std::runtime_error("unknown identifier: " + name);
    }

    Value subscript(const std::string& name, const Value& idx) {
        return subscript_impl(name, idx, ctx_);
    }

    static Value subscript_impl(const std::string& name, const Value& idx, ExpressionContext& ctx) {
        if (!ctx.has(name)) {
            throw std::runtime_error("unknown identifier in subscript: " + name);
        }
        Value var = ctx.retrieve(name);
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
        return call_function_impl(name, args, ctx_);
    }

    static Value call_function_impl(const std::string& name,
                                    const std::vector<Value>& args,
                                    ExpressionContext& ctx) {
        // First: user-defined function from namespace?
        if (ctx.has(name)) {
            Value v = ctx.retrieve(name);
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

// ============================================================================
// ExprNode + RDParser + evaluate_ast: parse-once, walk-many fast path for
// user-defined function definitions.
// ============================================================================
//
// The legacy path stored a function body as the literal source string and ran
// `ExpressionContext::eval(body)` on every call.  Profiling implicit.xml
// showed that ~30% of total runtime was being spent inside RDEval re-parsing
// the same 16-character string `"y^2 - x^3 + x"` on each QuadTree node visit
// (perf trace pruned_trace.json: parse_expr 30.44%, parse_term 28.00%,
// parse_unary 24.79%, strtod 4.26%, namespace lookups ~3%; actual `pow` only
// 3.6%).  This block fixes that bug at its root: we parse the function body
// once at definition time into a small AST, resolve identifiers that match
// the function's parameter names to ParamRef nodes (eliminating per-call
// hashmap probes for the bound arguments), and walk the AST against a
// fixed-size binding array on each call.
//
// The grammar is identical to RDEval's; only the semantic actions change
// (build nodes instead of computing Values eagerly).  RDEval is left intact
// as the legacy fallback for any expression RDParser cannot handle.

struct ExprNode {
    enum class Op : uint8_t {
        Const,        // k
        StringLit,    // str
        ParamRef,     // param_idx -- 0 or 1
        NamespaceRef, // str (identifier name)
        Neg,          // children[0]
        Add, Sub, Mul, Div, Pow,  // children[0], children[1]
        Call,         // str = function name, children = args
        Subscript,    // str = identifier name, children[0] = index
        VectorLit,    // children = elements (parenthesised tuple)
        ListLit,      // children = elements (bracketed list)
    };
    Op op;
    double k = 0.0;
    uint8_t param_idx = 0;
    std::string str;
    std::vector<std::unique_ptr<ExprNode>> children;

    explicit ExprNode(Op o) : op(o) {}
};

using ExprNodePtr = std::unique_ptr<ExprNode>;

// Recursive AST evaluator.  bindings is a pointer (may be nullptr if the
// AST contains no ParamRef nodes -- the parser only emits ParamRef when
// constructed with a non-empty params vector).
static Value evaluate_ast(const ExprNode& node,
                          const Value* bindings,
                          ExpressionContext& ctx) {
    switch (node.op) {
        case ExprNode::Op::Const:
            return Value(node.k);
        case ExprNode::Op::StringLit:
            return Value(node.str);
        case ExprNode::Op::ParamRef:
            return bindings[node.param_idx];
        case ExprNode::Op::NamespaceRef:
            return RDEval::lookup_ident_impl(node.str, ctx);
        case ExprNode::Op::Neg:
            return RDEval::vneg(evaluate_ast(*node.children[0], bindings, ctx));
        case ExprNode::Op::Add:
            return RDEval::vadd(evaluate_ast(*node.children[0], bindings, ctx),
                                evaluate_ast(*node.children[1], bindings, ctx));
        case ExprNode::Op::Sub:
            return RDEval::vsub(evaluate_ast(*node.children[0], bindings, ctx),
                                evaluate_ast(*node.children[1], bindings, ctx));
        case ExprNode::Op::Mul:
            return RDEval::vmul(evaluate_ast(*node.children[0], bindings, ctx),
                                evaluate_ast(*node.children[1], bindings, ctx));
        case ExprNode::Op::Div:
            return RDEval::vdiv(evaluate_ast(*node.children[0], bindings, ctx),
                                evaluate_ast(*node.children[1], bindings, ctx));
        case ExprNode::Op::Pow:
            return RDEval::vpow(evaluate_ast(*node.children[0], bindings, ctx),
                                evaluate_ast(*node.children[1], bindings, ctx));
        case ExprNode::Op::Call: {
            std::vector<Value> args;
            args.reserve(node.children.size());
            for (const auto& child : node.children) {
                args.push_back(evaluate_ast(*child, bindings, ctx));
            }
            return RDEval::call_function_impl(node.str, args, ctx);
        }
        case ExprNode::Op::Subscript: {
            Value idx = evaluate_ast(*node.children[0], bindings, ctx);
            return RDEval::subscript_impl(node.str, idx, ctx);
        }
        case ExprNode::Op::VectorLit: {
            std::vector<Value> elems;
            elems.reserve(node.children.size());
            for (const auto& child : node.children) {
                elems.push_back(evaluate_ast(*child, bindings, ctx));
            }
            if (elems.size() == 1) return elems[0];
            return RDEval::make_vector(elems);
        }
        case ExprNode::Op::ListLit: {
            std::vector<Value> elems;
            elems.reserve(node.children.size());
            for (const auto& child : node.children) {
                elems.push_back(evaluate_ast(*child, bindings, ctx));
            }
            return RDEval::make_list(elems);
        }
    }
    throw std::runtime_error("evaluate_ast: invalid op");
}

// ----------------------------------------------------------------------------
// Scalar fast path: evaluate_double
//
// The general evaluate_ast() above returns a `Value` (a 6-alternative
// std::variant ~64 bytes wide) at every step.  Profiling implicit.xml after
// items #1-#4 landed showed `_Variant_storage` + `_Copy_ctor_base` together
// at ~6.4% of total time -- the cost of constructing and destroying these
// variants on every recursive call inside the LevelSet::value hot loop.
//
// For the common case where a user function is purely scalar (Const,
// ParamRef, Neg/Add/Sub/Mul/Div/Pow over scalars, NamespaceRef to a double,
// and Call to a 1-arg builtin like sin/cos/exp/log/sqrt), there is no need
// for the variant at all -- the entire computation can stay in `double`.
// We add a parallel walker `evaluate_double` that returns `double` directly,
// and a predicate `is_scalar_only` that decides at AST-build time whether
// the body is eligible.  When eligible, try_define_function captures a
// fast-path closure that calls evaluate_double and only constructs a single
// Value at the boundary.  Otherwise the existing evaluate_ast path is used.
//
// This change is purely additive -- it cannot affect correctness for any
// expression that doesn't qualify, and for the qualifying cases the result
// is bit-identical to evaluate_ast (same std::pow / std::sin / etc.).

// Set of built-in 1-arg scalar functions that are safe to invoke from the
// double fast path.  This list mirrors the single-argument-scalar branch
// of RDEval::call_function_impl() above.  Functions that touch vectors,
// strings, or non-scalar Values must NOT appear here.
static bool is_scalar_builtin_1(const std::string& name) {
    static const std::unordered_set<std::string> scalars = {
        "sin", "cos", "tan", "asin", "acos", "atan",
        "sinh", "cosh", "tanh",
        "exp", "log", "ln", "log2", "log10",
        "sqrt", "cbrt",
        "abs", "fabs", "ceil", "floor", "round",
        "sec", "csc", "cot",
    };
    return scalars.find(name) != scalars.end();
}

// 2-arg scalar builtins.
static bool is_scalar_builtin_2(const std::string& name) {
    static const std::unordered_set<std::string> scalars = {
        "atan2", "pow", "fmod", "max", "min",
    };
    return scalars.find(name) != scalars.end();
}

// Decide whether an AST is eligible for the double fast path.  An AST is
// scalar-only iff every node:
//   - is a Const, ParamRef, NamespaceRef-to-a-double, Neg, Add, Sub, Mul,
//     Div, or Pow, or
//   - is a Call to a known scalar-builtin with the right arity, where every
//     argument is itself scalar-only.
//
// Subscripts, vector literals, list literals, string literals, and calls to
// user-defined functions or vector builtins are all rejected.  We err on
// the conservative side: if anything is unrecognised, the function falls
// back to evaluate_ast.
//
// `ctx` is needed to check that NamespaceRef nodes resolve to a double
// constant (e.g. `pi`, `e`, or a user-defined scalar like `k = 0.5`).  If
// the namespace value is anything other than a scalar, we reject.
static bool is_scalar_only(const ExprNode& node, const ExpressionContext& ctx) {
    switch (node.op) {
        case ExprNode::Op::Const:
        case ExprNode::Op::ParamRef:
            return true;
        case ExprNode::Op::NamespaceRef: {
            // Built-in math constants always exist
            if (node.str == "pi" || node.str == "e" || node.str == "inf") return true;
            // Otherwise look up in namespace_; must be a scalar Value
            if (!ctx.has(node.str)) return false;
            try {
                Value v = ctx.retrieve(node.str);
                return v.is_double();
            } catch (...) {
                return false;
            }
        }
        case ExprNode::Op::Neg:
            return is_scalar_only(*node.children[0], ctx);
        case ExprNode::Op::Add:
        case ExprNode::Op::Sub:
        case ExprNode::Op::Mul:
        case ExprNode::Op::Div:
        case ExprNode::Op::Pow:
            return is_scalar_only(*node.children[0], ctx) &&
                   is_scalar_only(*node.children[1], ctx);
        case ExprNode::Op::Call: {
            const auto arity = node.children.size();
            if (arity == 1 && is_scalar_builtin_1(node.str)) {
                return is_scalar_only(*node.children[0], ctx);
            }
            if (arity == 2 && is_scalar_builtin_2(node.str)) {
                return is_scalar_only(*node.children[0], ctx) &&
                       is_scalar_only(*node.children[1], ctx);
            }
            return false;
        }
        case ExprNode::Op::Subscript:
        case ExprNode::Op::VectorLit:
        case ExprNode::Op::ListLit:
        case ExprNode::Op::StringLit:
            return false;
    }
    return false;
}

// The double-only walker.  Mirrors evaluate_ast() exactly for the scalar
// subset, but never constructs a Value.  All variants of std::pow / std::sin
// / etc. are the same library calls evaluate_ast would make via the
// RDEval::call_function_impl dispatch, so the numerical results are
// guaranteed to match bit-for-bit.
static double evaluate_double(const ExprNode& node,
                              const double* bindings,
                              ExpressionContext& ctx) {
    switch (node.op) {
        case ExprNode::Op::Const:
            return node.k;
        case ExprNode::Op::ParamRef:
            return bindings[node.param_idx];
        case ExprNode::Op::NamespaceRef: {
            if (node.str == "pi") return M_PI;
            if (node.str == "e") return M_E;
            if (node.str == "inf") return std::numeric_limits<double>::infinity();
            // Caller has guaranteed (via is_scalar_only) that this exists
            // and is a double, so a hashmap lookup here is safe.
            return ctx.retrieve(node.str).as_double();
        }
        case ExprNode::Op::Neg:
            return -evaluate_double(*node.children[0], bindings, ctx);
        case ExprNode::Op::Add:
            return evaluate_double(*node.children[0], bindings, ctx) +
                   evaluate_double(*node.children[1], bindings, ctx);
        case ExprNode::Op::Sub:
            return evaluate_double(*node.children[0], bindings, ctx) -
                   evaluate_double(*node.children[1], bindings, ctx);
        case ExprNode::Op::Mul:
            return evaluate_double(*node.children[0], bindings, ctx) *
                   evaluate_double(*node.children[1], bindings, ctx);
        case ExprNode::Op::Div:
            return evaluate_double(*node.children[0], bindings, ctx) /
                   evaluate_double(*node.children[1], bindings, ctx);
        case ExprNode::Op::Pow:
            return std::pow(evaluate_double(*node.children[0], bindings, ctx),
                            evaluate_double(*node.children[1], bindings, ctx));
        case ExprNode::Op::Call: {
            const auto& name = node.str;
            if (node.children.size() == 1) {
                double a = evaluate_double(*node.children[0], bindings, ctx);
                if (name == "sin")   return std::sin(a);
                if (name == "cos")   return std::cos(a);
                if (name == "tan")   return std::tan(a);
                if (name == "asin")  return std::asin(a);
                if (name == "acos")  return std::acos(a);
                if (name == "atan")  return std::atan(a);
                if (name == "sinh")  return std::sinh(a);
                if (name == "cosh")  return std::cosh(a);
                if (name == "tanh")  return std::tanh(a);
                if (name == "exp")   return std::exp(a);
                if (name == "log")   return std::log(a);
                if (name == "ln")    return std::log(a);
                if (name == "log2")  return std::log2(a);
                if (name == "log10") return std::log10(a);
                if (name == "sqrt")  return std::sqrt(a);
                if (name == "cbrt")  return std::cbrt(a);
                if (name == "abs")   return std::fabs(a);
                if (name == "fabs")  return std::fabs(a);
                if (name == "ceil")  return std::ceil(a);
                if (name == "floor") return std::floor(a);
                if (name == "round") return std::round(a);
                if (name == "sec")   return prefigure::sec(a);
                if (name == "csc")   return prefigure::csc(a);
                if (name == "cot")   return prefigure::cot(a);
            } else if (node.children.size() == 2) {
                double a = evaluate_double(*node.children[0], bindings, ctx);
                double b = evaluate_double(*node.children[1], bindings, ctx);
                if (name == "atan2") return std::atan2(a, b);
                if (name == "pow")   return std::pow(a, b);
                if (name == "fmod")  return std::fmod(a, b);
                if (name == "max")   return std::max(a, b);
                if (name == "min")   return std::min(a, b);
            }
            // Should be unreachable thanks to is_scalar_only's gating, but
            // fall through to a clear error if a future code path adds a new
            // builtin to is_scalar_builtin_* without updating this dispatch.
            throw std::runtime_error("evaluate_double: unsupported call: " + name);
        }
        // is_scalar_only rejects these; reaching them indicates a bug.
        case ExprNode::Op::Subscript:
        case ExprNode::Op::VectorLit:
        case ExprNode::Op::ListLit:
        case ExprNode::Op::StringLit:
            throw std::runtime_error("evaluate_double: non-scalar op in scalar fast path");
    }
    throw std::runtime_error("evaluate_double: invalid op");
}

// Recursive-descent parser that produces an ExprNode AST.  Identical grammar
// to RDEval; semantic actions build nodes instead of evaluating eagerly.
//
// If `params` is non-null and non-empty, identifiers whose names match a
// param entry are emitted as ParamRef{idx} -- this is what makes the per-call
// path skip the namespace hashmap entirely for f(x,y)'s bound arguments.
class RDParser {
public:
    RDParser(const std::string& src, const std::vector<std::string>* params)
        : src_(src), pos_(0), params_(params) {}

    ExprNodePtr parse_top() {
        skip_ws();
        if (pos_ >= src_.size()) {
            throw std::runtime_error("empty expression");
        }
        ExprNodePtr first = parse_expr();
        skip_ws();
        // Bare top-level tuple: a, b, c
        if (pos_ < src_.size() && src_[pos_] == ',') {
            auto vec = std::make_unique<ExprNode>(ExprNode::Op::VectorLit);
            vec->children.push_back(std::move(first));
            while (pos_ < src_.size() && src_[pos_] == ',') {
                ++pos_;
                vec->children.push_back(parse_expr());
                skip_ws();
            }
            first = std::move(vec);
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
    const std::vector<std::string>* params_;

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

    static ExprNodePtr make_binop(ExprNode::Op op, ExprNodePtr lhs, ExprNodePtr rhs) {
        auto n = std::make_unique<ExprNode>(op);
        n->children.push_back(std::move(lhs));
        n->children.push_back(std::move(rhs));
        return n;
    }

    ExprNodePtr parse_expr() {
        ExprNodePtr left = parse_term();
        while (true) {
            skip_ws();
            char c = peek();
            if (c == '+') { ++pos_; left = make_binop(ExprNode::Op::Add, std::move(left), parse_term()); }
            else if (c == '-') { ++pos_; left = make_binop(ExprNode::Op::Sub, std::move(left), parse_term()); }
            else break;
        }
        return left;
    }

    ExprNodePtr parse_term() {
        ExprNodePtr left = parse_unary();
        while (true) {
            skip_ws();
            // Avoid consuming ** as two separate * tokens
            if (peek() == '*' && peek(1) == '*') break;
            char c = peek();
            if (c == '*') { ++pos_; left = make_binop(ExprNode::Op::Mul, std::move(left), parse_unary()); }
            else if (c == '/') { ++pos_; left = make_binop(ExprNode::Op::Div, std::move(left), parse_unary()); }
            else break;
        }
        return left;
    }

    ExprNodePtr parse_unary() {
        skip_ws();
        if (consume_char('+')) return parse_unary();
        if (consume_char('-')) {
            auto n = std::make_unique<ExprNode>(ExprNode::Op::Neg);
            n->children.push_back(parse_unary());
            return n;
        }
        return parse_power();
    }

    ExprNodePtr parse_power() {
        ExprNodePtr base = parse_primary();
        skip_ws();
        if (consume_str("**") || consume_char('^')) {
            ExprNodePtr exp = parse_unary();   // right-associative

            // Peephole optimisation: small constant integer exponents become
            // a left-associative Mul chain so the inner loop avoids the
            // expensive __ieee754_pow_fma call entirely.  In the implicit.xml
            // body `y^2 - x^3 + x`, this rewrites both subterms.  The trace
            // showed `pow` at 5.6% self-time even after the AST walker landed.
            //
            // Constraints on the rewrite:
            //   - exponent must be an integer Const node (not a variable)
            //   - 2 <= n <= 6  (small enough that Mul chains don't bloat the
            //     AST disproportionately, big enough to cover the common
            //     cases of squaring and cubing)
            //   - we cannot fold n=0 (always 1) or n=1 (always base) at this
            //     stage without also evaluating side effects in `base`; the
            //     existing AST walker handles those correctly via Pow anyway.
            if (exp->op == ExprNode::Op::Const) {
                double k = exp->k;
                int ki = static_cast<int>(k);
                if (k == static_cast<double>(ki) && ki >= 2 && ki <= 6) {
                    // We need `ki` copies of `base`.  Clone the original base
                    // subtree once into a template, then build a left-
                    // associative Mul chain by repeatedly cloning the template
                    // for the right operand.  E.g. for ki=3:
                    //   base_template = clone(base)
                    //   result = Mul(base, clone(base_template))           // x*x
                    //   result = Mul(result, clone(base_template))         // (x*x)*x
                    //
                    // Cloning the original base each iteration (rather than
                    // the running `result`) is what makes this O(ki * |base|)
                    // rather than O(2^ki * |base|).
                    auto base_template = clone_node(*base);
                    ExprNodePtr result = make_binop(ExprNode::Op::Mul,
                                                    std::move(base),
                                                    clone_node(*base_template));
                    for (int i = 2; i < ki; ++i) {
                        result = make_binop(ExprNode::Op::Mul,
                                            std::move(result),
                                            clone_node(*base_template));
                    }
                    return result;
                }
            }

            return make_binop(ExprNode::Op::Pow, std::move(base), std::move(exp));
        }
        return base;
    }

    // Deep-copy an ExprNode subtree.  Used by the pow->mul peephole above
    // when an integer exponent requires multiple copies of the base.
    static ExprNodePtr clone_node(const ExprNode& src) {
        auto out = std::make_unique<ExprNode>(src.op);
        out->k = src.k;
        out->param_idx = src.param_idx;
        out->str = src.str;
        out->children.reserve(src.children.size());
        for (const auto& c : src.children) {
            out->children.push_back(clone_node(*c));
        }
        return out;
    }

    ExprNodePtr parse_primary() {
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
            std::vector<ExprNodePtr> elems;
            elems.push_back(parse_expr());
            skip_ws();
            while (consume_char(',')) {
                elems.push_back(parse_expr());
                skip_ws();
            }
            if (!consume_char(')')) {
                throw std::runtime_error("expected ')'");
            }
            if (elems.size() == 1) return std::move(elems[0]);
            auto n = std::make_unique<ExprNode>(ExprNode::Op::VectorLit);
            n->children = std::move(elems);
            return n;
        }
        if (c == '[') {
            ++pos_;
            std::vector<ExprNodePtr> elems;
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
            auto n = std::make_unique<ExprNode>(ExprNode::Op::ListLit);
            n->children = std::move(elems);
            return n;
        }
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            return parse_ident_or_call();
        }
        throw std::runtime_error(std::string("unexpected character: '") + c + "'");
    }

    ExprNodePtr parse_list_item() {
        skip_ws();
        if (peek() == '\'' || peek() == '"') return parse_string_literal();
        return parse_expr();
    }

    ExprNodePtr parse_number() {
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
        auto n = std::make_unique<ExprNode>(ExprNode::Op::Const);
        n->k = std::stod(src_.substr(start, pos_ - start));
        return n;
    }

    ExprNodePtr parse_string_literal() {
        // Match Python's string-literal escape rules; same logic as RDEval.
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
        auto n = std::make_unique<ExprNode>(ExprNode::Op::StringLit);
        n->str = std::move(s);
        return n;
    }

    // Parse an identifier or function call.  This is the place where the
    // parameter-resolution optimisation happens.
    ExprNodePtr parse_ident_or_call() {
        size_t start = pos_;
        while (pos_ < src_.size() &&
               (std::isalnum(static_cast<unsigned char>(src_[pos_])) || src_[pos_] == '_'))
            ++pos_;
        std::string name = src_.substr(start, pos_ - start);

        skip_ws();
        if (peek() == '(') {
            // Function call
            ++pos_;
            std::vector<ExprNodePtr> args;
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
            auto n = std::make_unique<ExprNode>(ExprNode::Op::Call);
            n->str = std::move(name);
            n->children = std::move(args);
            return n;
        }
        if (peek() == '[') {
            // Subscript
            ++pos_;
            ExprNodePtr idx = parse_expr();
            skip_ws();
            if (!consume_char(']')) {
                throw std::runtime_error("expected ']' in subscript " + name);
            }
            auto n = std::make_unique<ExprNode>(ExprNode::Op::Subscript);
            n->str = std::move(name);
            n->children.push_back(std::move(idx));
            return n;
        }

        // Plain identifier.  If it matches one of the surrounding function's
        // parameters, emit ParamRef -- this is the key optimization that
        // eliminates the per-call hashmap lookup for f(x,y)'s bound args.
        if (params_) {
            for (size_t i = 0; i < params_->size(); ++i) {
                if ((*params_)[i] == name) {
                    auto n = std::make_unique<ExprNode>(ExprNode::Op::ParamRef);
                    n->param_idx = static_cast<uint8_t>(i);
                    return n;
                }
            }
        }
        auto n = std::make_unique<ExprNode>(ExprNode::Op::NamespaceRef);
        n->str = std::move(name);
        return n;
    }
};

}  // anonymous namespace

// ----------------------------------------------------------------------------
// CompiledScalar2State: per-function compiled state for the
// CompiledFunction2 raw-function-pointer dispatcher.
//
// Held in ExpressionContext::compiled_scalar2_ as a unique_ptr (for stable
// addresses), and pointed to by the impl_ field of every CompiledFunction2
// returned from get_compiled_scalar2().  The dispatcher does a single
// pointer cast and forwards to evaluate_double().
//
// Defined at namespace scope (not inside the anonymous namespace) because
// the header forward-declares it.

struct CompiledScalar1State {
    std::shared_ptr<ExprNode> ast;
    ExpressionContext* ctx;

    CompiledScalar1State(std::shared_ptr<ExprNode> a, ExpressionContext* c)
        : ast(std::move(a)), ctx(c) {}
};

struct CompiledScalar2State {
    std::shared_ptr<ExprNode> ast;
    ExpressionContext* ctx;

    CompiledScalar2State(std::shared_ptr<ExprNode> a, ExpressionContext* c)
        : ast(std::move(a)), ctx(c) {}
};

// 1-arg dispatcher.  Mirror of dispatch_compiled_scalar2 for the single-
// argument case.  Used by graph.cpp / slope_field.cpp / etc. via the
// CompiledFunction1 raw-fn-pointer wrapper.
static double dispatch_compiled_scalar1(const void* impl, double x) {
    const auto* state = static_cast<const CompiledScalar1State*>(impl);
    double bindings[1] = { x };
    return evaluate_double(*state->ast, bindings, *state->ctx);
}

// The dispatcher is a free function with C-compatible signature so its
// address can be stored in the CompiledFunction2's invoke_ field.  No
// virtual dispatch, no std::function overhead, no exception in the hot path
// (evaluate_double only throws on bug-condition unreachable cases).
static double dispatch_compiled_scalar2(const void* impl, double x, double y) {
    const auto* state = static_cast<const CompiledScalar2State*>(impl);
    double bindings[2] = { x, y };
    return evaluate_double(*state->ast, bindings, *state->ctx);
}

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

                // Parse-once fast path: try to compile the function body
                // into an ExprNode AST.  If parsing succeeds, the closure
                // captures a shared_ptr<ExprNode> and walks the tree on each
                // call -- no per-call re-parsing, no per-call namespace
                // hashmap probes for the bound parameters.  If parsing fails
                // for any reason, we fall back to the legacy
                // capture-string-and-eval closure below, so any expression
                // RDParser cannot handle still works exactly as before.
                std::shared_ptr<ExprNode> compiled_body;
                try {
                    RDParser parser(body, &arg_names);
                    compiled_body = std::shared_ptr<ExprNode>(parser.parse_top().release());
                } catch (const std::exception& e) {
                    spdlog::debug("RDParser could not pre-compile '{}' for {}: {} -- "
                                  "falling back to per-call eval",
                                  body, func_name, e.what());
                    compiled_body.reset();
                }

                // If the AST is a pure-scalar expression we can use the
                // double fast path, which avoids constructing Value variants
                // on every recursive node visit.  Decided once here at
                // definition time so the per-call closure doesn't have to
                // re-check anything.
                const bool scalar_fast =
                    compiled_body && is_scalar_only(*compiled_body, *this);

                if (arg_names.size() == 1) {
                    auto* ctx = this;

                    if (scalar_fast) {
                        // Fast-path: scalar AST walked as plain double.  The
                        // boundary still has to convert Value <-> double for
                        // the MathFunction signature, but the heavy
                        // recursion never touches a variant.
                        MathFunction func = [ctx, compiled_body](Value arg_val) -> Value {
                            double bindings[1] = { arg_val.to_double() };
                            return Value(evaluate_double(*compiled_body, bindings, *ctx));
                        };
                        functions_.insert(func_name);
                        variables_.insert(func_name);
                        namespace_[func_name] = Value(func);

                        // Also publish a CompiledFunction1 view of the same
                        // AST under the same name so element handlers like
                        // graph.cpp / parametric_curve.cpp / slope_field.cpp
                        // (scalar branch) can opt into the raw-function-
                        // pointer dispatch and skip the std::function shim
                        // entirely on their hot inner loops.
                        compiled_scalar1_[func_name] =
                            std::make_unique<CompiledScalar1State>(compiled_body, ctx);

                        return Value(func);
                    }

                    if (compiled_body) {
                        // Slow path: parse-once AST walk that returns Value.
                        // Used when the body involves vectors, subscripts,
                        // user-defined function calls, or any non-scalar op.
                        MathFunction func = [ctx, compiled_body](Value arg_val) -> Value {
                            Value bindings[1] = { std::move(arg_val) };
                            return evaluate_ast(*compiled_body, bindings, *ctx);
                        };
                        functions_.insert(func_name);
                        variables_.insert(func_name);
                        namespace_[func_name] = Value(func);
                        return Value(func);
                    }

                    // Legacy path: capture source string, re-parse on each call
                    auto captured_body = body;
                    auto captured_arg = arg_names[0];
                    MathFunction func = [ctx, captured_body, captured_arg](Value arg_val) -> Value {
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
                    auto* ctx = this;

                    if (scalar_fast) {
                        // Scalar fast path for 2-arg functions like the
                        // implicit-curve level set f(x,y) = y^2 - x^3 + x.
                        // The hot inner loop in implicit.cpp / slope_field.cpp
                        // / diffeqs.cpp now walks the AST in pure double,
                        // skipping all Value variant copy/destroy overhead.
                        MathFunction2 func = [ctx, compiled_body](Value a1, Value a2) -> Value {
                            double bindings[2] = { a1.to_double(), a2.to_double() };
                            return Value(evaluate_double(*compiled_body, bindings, *ctx));
                        };
                        functions_.insert(func_name);
                        variables_.insert(func_name);
                        namespace_[func_name] = Value(func);

                        // Also publish a CompiledFunction2 view of the same
                        // AST under the same name, so hot inner loops in
                        // element handlers can opt into the raw-function-
                        // pointer dispatch (no std::function shim, no Value
                        // copy/destroy at the boundary).  See
                        // ExpressionContext::get_compiled_scalar2() and
                        // implicit.cpp::implicit_curve.
                        compiled_scalar2_[func_name] =
                            std::make_unique<CompiledScalar2State>(compiled_body, ctx);

                        return Value(func);
                    }

                    if (compiled_body) {
                        // Slow (general) AST path for 2-arg functions whose
                        // body uses vectors, subscripts, etc.  Notably this
                        // is the path de-system.xml takes because its ODE
                        // RHS returns a 2-vector.
                        MathFunction2 func = [ctx, compiled_body](Value a1, Value a2) -> Value {
                            Value bindings[2] = { std::move(a1), std::move(a2) };
                            return evaluate_ast(*compiled_body, bindings, *ctx);
                        };
                        functions_.insert(func_name);
                        variables_.insert(func_name);
                        namespace_[func_name] = Value(func);
                        return Value(func);
                    }

                    // Legacy path: capture source string, re-parse on each call
                    auto captured_body = body;
                    auto captured_args = arg_names;
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

    // Try scalar evaluation with exprtk.
    //
    // We use a cached `exprtk::parser<double>` (lazily created on first
    // call) so we don't pay the parser-construction cost (~3% of total time
    // in the implicit profile pre-Phase-1.5) on every label/attribute eval.
    // The symbol_table is still built fresh per call -- it's cheap by
    // comparison and avoids the bookkeeping needed to keep cached variable
    // bindings in sync with namespace_ across calls.
    if (!exprtk_state_) {
        exprtk_state_ = std::make_unique<ExprtkState>();
    }
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

        if (exprtk_state_->parser.compile(processed_expr, expression)) {
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
    // The pattern for `name[index]` never changes, so compile it exactly once.
    // Without this, std::regex's compiler ran on every label/attribute eval and
    // showed up at ~7% of total runtime in the implicit.xml profile (the
    // _M_disjunction / _M_alternative / _M_bracket_expression chain in
    // libstdc++).  function-local static is initialised exactly once and is
    // thread-safe under C++11+.
    static const std::regex sub_re(R"(([a-zA-Z_]\w*)\[([^\]]+)\])");

    std::string result = expr;
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

// Helper that turns a possibly-whitespaced expression string into a bare
// identifier name, or returns std::nullopt if the input is anything more
// elaborate (parens, operators, etc.).  Used by get_compiled_scalar1 and
// get_compiled_scalar2.
static std::optional<std::string> as_bare_identifier(const std::string& s) {
    size_t first = s.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return std::nullopt;
    size_t last = s.find_last_not_of(" \t\n\r");
    std::string name = s.substr(first, last - first + 1);
    if (name.empty()) return std::nullopt;
    if (!std::isalpha(static_cast<unsigned char>(name[0])) && name[0] != '_') {
        return std::nullopt;
    }
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
            return std::nullopt;
        }
    }
    return name;
}

std::optional<CompiledFunction1>
ExpressionContext::get_compiled_scalar1(const std::string& name_or_expr) const {
    auto name = as_bare_identifier(name_or_expr);
    if (!name) return std::nullopt;

    auto it = compiled_scalar1_.find(*name);
    if (it == compiled_scalar1_.end()) return std::nullopt;

    CompiledFunction1 cf;
    cf.impl_ = it->second.get();
    cf.invoke_ = &dispatch_compiled_scalar1;
    return cf;
}

std::optional<CompiledFunction2>
ExpressionContext::get_compiled_scalar2(const std::string& name_or_expr) const {
    // Trim leading/trailing whitespace.  Anything more elaborate (parens,
    // operators, function calls of functions) means the caller wants to
    // evaluate an expression, not look up a single named function, and we
    // return nullopt so the caller falls back to the regular MathFunction2.
    auto name = as_bare_identifier(name_or_expr);
    if (!name) return std::nullopt;

    auto it = compiled_scalar2_.find(*name);
    if (it == compiled_scalar2_.end()) return std::nullopt;

    CompiledFunction2 cf;
    cf.impl_ = it->second.get();
    cf.invoke_ = &dispatch_compiled_scalar2;
    return cf;
}

}  // namespace prefigure
