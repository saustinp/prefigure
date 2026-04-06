Expression Evaluator
====================

The expression evaluator is the most architecturally significant difference
between the Python and C++ implementations. Python uses ``ast.parse()`` +
``eval()`` with dynamic lambda creation; C++ uses the
`exprtk <https://www.partow.net/programming/exprtk/>`_ library with a custom
pre-parser for tuple literals and function definitions.

Overview
--------

The ``ExpressionContext`` class (``user_namespace.hpp``) provides safe evaluation
of mathematical expressions authored in XML diagram source. Each ``Diagram``
owns a fresh ``ExpressionContext`` instance, replacing Python's module-level
globals that were reset via ``importlib.reload()``.

Supported Expression Types
--------------------------

**Scalar arithmetic**::

    "3 + 4 * 2"          → 11.0
    "sin(pi/4)"          → 0.7071...
    "2^3"                → 8.0       (exprtk uses ^ for power natively)

**Variable definitions**::

    ctx.define("a = 5");
    ctx.eval("a + 3");   → 8.0

**Function definitions**::

    ctx.define("f(x) = x^2 + 3*x + 1");
    ctx.eval("f(5)");    → 41.0

**Two-argument functions** (for ODEs)::

    ctx.define("g(t, y) = t - y");
    // Called as: g(Value(0.5), Value(1.0))

**Tuple/vector literals**::

    ctx.eval("(1, 2, 3)");   → Eigen::VectorXd [1, 2, 3]
    ctx.eval("[4, 5]");      → Eigen::VectorXd [4, 5]

**Color strings** (passed through without evaluation)::

    ctx.eval("#ff0000");      → string "#ff0000"
    ctx.eval("rgb(255,0,0)"); → string "rgb(255,0,0)"

How It Works Internally
-----------------------

When ``eval(expr)`` is called:

1. **Preprocessing**: Trim whitespace. Check for color strings (``#`` or ``rgb``
   prefix) and return as-is.

2. **Assignment detection**: If ``=`` is found (not ``==``, ``<=``, ``>=``):

   - If left-hand side has parentheses → **function definition**:
     Parse argument names, capture the body expression, create a
     ``std::function`` closure that evaluates the body with arguments bound.

   - Otherwise → **variable assignment**: Evaluate right-hand side, store
     result under the variable name.

3. **Tuple/vector detection**: If the expression starts with ``(`` or ``[``
   and contains commas at depth 0, split into elements, evaluate each
   recursively, pack into ``Eigen::VectorXd``.

4. **Namespace lookup**: Check if the expression is a known variable name
   and return its stored value.

5. **Scalar evaluation**: Compile the expression with exprtk using all
   current namespace scalars as variables. Evaluate and return the result.

6. **Fallback**: If nothing matches, return the expression as a plain string.

Differences from Python
------------------------

.. list-table::
   :header-rows: 1
   :widths: 35 35 30

   * - Feature
     - Python
     - C++ (exprtk)
   * - Power operator
     - ``^`` replaced with ``**``
     - ``^`` is native power
   * - List/tuple creation
     - ``eval()`` creates Python lists
     - Custom pre-parser → ``Eigen::VectorXd``
   * - Function definitions
     - Dynamic ``lambda`` via ``compile()``
     - ``std::function`` closure
   * - Namespace storage
     - ``globals()`` dict
     - ``ExpressionContext::namespace_`` map
   * - Namespace reset
     - ``importlib.reload()``
     - New ``ExpressionContext`` per diagram
   * - Array indexing (``a[k]``)
     - Native Python
     - Custom pre-parser (partial)
   * - Dict literals (``{}``)
     - Native Python ``eval()``
     - Not yet supported
   * - Security model
     - AST whitelist validation
     - exprtk is inherently safe (math only)

Known Limitations
-----------------

- **No dictionary literals**: Expressions like ``{'a': 1, 'b': 2}`` (used by
  ``network.py``'s ``label-dictionary`` attribute) are not supported. This will
  need a custom mini-parser when ``network.cpp`` is fully implemented.

- **No list comprehensions**: Python expressions like ``[f(k) for k in range(5)]``
  cannot be evaluated. Use ``<repeat>`` elements instead.

- **No imports or arbitrary code**: Only mathematical expressions are supported.
  This is by design — it's a security feature.

- **Array indexing is limited**: Simple ``a[k]`` works for namespace lookups
  but complex indexing (``a[k+1]``, slicing) may not.

Built-in Functions
------------------

All standard math functions are available: ``sin``, ``cos``, ``tan``, ``asin``,
``acos``, ``atan``, ``atan2``, ``exp``, ``log``, ``log2``, ``log10``, ``sqrt``,
``abs``, ``ceil``, ``floor``, ``round``, ``pow``, ``max``, ``min``.

Additional functions from ``math_utilities.hpp``:

- **Vector ops**: ``dot(u,v)``, ``distance(p,q)``, ``length(u)``,
  ``normalize(u)``, ``midpoint(u,v)``, ``angle(p)``
- **Trig**: ``ln(x)``, ``sec(x)``, ``csc(x)``, ``cot(x)``
- **Calculus**: ``deriv(f, a)`` (numerical derivative)
- **Combinatorics**: ``choose(n, k)``
- **Indicators**: ``chi_oo(a,b,t)``, ``chi_oc``, ``chi_co``, ``chi_cc``
- **Geometry**: ``rotate(v, theta)``, ``evaluate_bezier(controls, t)``,
  ``line_intersection(p1, p2, q1, q2)``, ``intersect(f, seed, min, max)``

Built-in constants: ``pi``, ``e``, ``inf``.

Expression Preprocessing Pipeline
----------------------------------

Before an expression reaches exprtk, it passes through several pre-processing
steps.  Understanding this pipeline is important when debugging evaluation
issues or extending the expression system.

**1. Power operator substitution** (``**`` → ``^``)

Python uses ``**`` for exponentiation; exprtk uses ``^``.  The pre-processor
replaces all occurrences of ``**`` with ``^`` so that authors can use either
syntax in their XML::

    "x**2 + 3"  →  "x^2 + 3"

**2. Array subscript replacement** (``replace_array_subscripts``)

Expressions like ``a[k]`` are not natively supported by exprtk.  The
pre-processor scans for patterns matching ``name[index]``, evaluates the
index, retrieves the vector from the namespace, and substitutes the scalar
value::

    // Given: a = (10, 20, 30) and k = 1
    "a[k] + 5"  →  "20 + 5"

Only simple indexing is supported — ``a[k+1]`` or slicing is not.

**3. Function call replacement** (``replace_function_calls``)

User-defined functions (created via ``define("f(x) = x^2")``) are stored as
``std::function`` closures in the namespace, not as exprtk symbols.  The
pre-processor detects calls to these functions, evaluates them, and
substitutes the numeric result::

    // Given: f(x) = x^2
    "3 * f(4) + 1"  →  "3 * 16 + 1"

This allows exprtk to evaluate the remaining arithmetic.  For nested calls
or multi-argument functions, the pre-processor recursively evaluates from
the innermost call outward.

Break Detection for ODEs
-------------------------

The expression context supports "break detection" for ODE solvers that need
to handle piecewise-defined functions (e.g., step functions, delta functions).

**Workflow:**

1. Enable break collection: ``find_breaks(f, t0, y0)`` sets ``delta_on_ = true``
   and evaluates the ODE right-hand side.  During this evaluation, any call
   to the built-in ``delta(t, a)`` function records ``a`` in ``breaks_``.

2. Sort and integrate piecewise: The ODE solver sorts the collected break
   points and integrates between consecutive pairs.

3. At each break, ``measure_de_jump(f, t, y)`` evaluates the function with
   ``delta_on_ = true`` to compute the magnitude of the discontinuity,
   which is added to the state vector before continuing.

This mechanism is used by ``diffeqs.cpp`` to handle the ``delta(t, a)``
built-in, enabling step-function forcing in ODEs.

Custom Function Dispatch
-------------------------

The expression evaluator supports three kinds of callable:

- **Scalar functions** (``MathFunction``): ``f(Value) → Value``.
  Defined via ``"f(x) = expr"`` or registered programmatically.

- **Two-argument functions** (``MathFunction2``): ``f(Value, Value) → Value``.
  Defined via ``"g(t, y) = expr"``.  Used primarily by ODE solvers and
  implicit curve handlers.

- **Built-in math functions**: Registered during ``init_builtins()`` and
  available in all expressions.  Includes both standard math (sin, cos, etc.)
  and domain-specific functions (deriv, choose, chi indicators, etc.)

When ``eval()`` encounters a function call, it checks in order:
namespace functions → built-in functions → exprtk evaluation.  This
precedence allows user definitions to shadow built-ins when needed.
