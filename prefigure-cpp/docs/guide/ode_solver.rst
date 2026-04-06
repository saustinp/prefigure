ODE Solver
==========

The ``diffeqs.cpp`` module provides numerical ODE solving for the ``<de-solve>``
and ``<plot-de-solution>`` XML elements. It replaces Python's
``scipy.integrate.solve_ivp`` with a custom Dormand-Prince RK45 adaptive
stepper implemented directly in C++.

Algorithm: Dormand-Prince RK45
------------------------------

The solver uses the Dormand-Prince method, which is the same algorithm behind
scipy's ``RK45`` and MATLAB's ``ode45``. It is an explicit Runge-Kutta method
of order 4(5) with an embedded error estimator.

**Key properties:**

- 7 stages per step (FSAL: First Same As Last — the 7th stage of step *n*
  becomes the 1st stage of step *n+1*, saving one function evaluation)
- 4th-order solution for stepping, 5th-order solution for error estimation
- Adaptive step size control with safety factor 0.9
- Tolerances: ``atol = 1e-6``, ``rtol = 1e-6``

**Step size control:**

After each step, the error norm is computed as:

.. math::

   \text{err\_norm} = \sqrt{ \frac{1}{n} \sum_{i=1}^{n}
   \left( \frac{\text{err}_i}{\text{atol} + \text{rtol} \cdot
   \max(|y_i|, |y_{\text{new},i}|)} \right)^2 }

If ``err_norm <= 1``, the step is accepted and the step size is increased:

.. math::

   h_{\text{new}} = 0.9 \cdot h \cdot \text{err\_norm}^{-0.2}

If ``err_norm > 1``, the step is rejected and retried with reduced size:

.. math::

   h_{\text{new}} = 0.9 \cdot h \cdot \text{err\_norm}^{-0.25}

Dense Output via Cubic Hermite Interpolation
--------------------------------------------

The solver produces output at user-specified evaluation times (typically
N=100 equally spaced points) using cubic Hermite interpolation between
accepted steps. This uses the function values at both endpoints of each
step interval, which are already available from the FSAL property:

.. math::

   y(t) = (1-s) \, y_n + s \, y_{n+1} + s(s-1) \big[ (1-2s)(y_{n+1}-y_n)
   + h \big( (s-1) f_n + s \, f_{n+1} \big) \big]

where :math:`s = (t - t_n) / h` and :math:`h = t_{n+1} - t_n`.

This produces smooth curves without the kinks that linear interpolation
would introduce.

Discontinuity Handling
----------------------

The solver supports piecewise integration around discontinuities (delta
functions in the ODE right-hand side). The workflow:

1. **Break detection**: Call ``ExpressionContext::find_breaks(f, t0, y0)``
   which evaluates ``f(t0, y0)`` with break collection enabled. Any calls
   to ``delta(t, a)`` during this evaluation record ``a`` as a break point.

2. **Piecewise integration**: Sort breaks and integrate between them:
   ``[t0, break1]``, ``[break1, break2]``, ..., ``[last_break, t1]``

3. **Jump application**: At each break point, call
   ``ExpressionContext::measure_de_jump(f, t, y)`` to compute the
   discontinuity magnitude, then adjust ``y0`` for the next segment.

Solution Storage
----------------

The solution is stored as an ``Eigen::MatrixXd`` in the expression namespace.
The matrix layout matches Python's ``np.stack((t, *y))``:

- Row 0: time values ``t``
- Row 1: first state variable ``y0``
- Row 2: second state variable ``y1`` (for systems)
- etc.

This is accessed by ``plot_de_solution()`` which maps axis names to row
indices: ``t`` → row 0, ``y`` or ``y0`` → row 1, ``y1`` → row 2, etc.

Differences from scipy
-----------------------

.. list-table::
   :header-rows: 1

   * - Feature
     - scipy
     - C++ (prefigure)
   * - Algorithm
     - Dormand-Prince 4(5)
     - Dormand-Prince 4(5) (identical Butcher tableau)
   * - Dense output
     - 4th-order Hermite using RK stages
     - Cubic Hermite using f(t_n) and f(t_{n+1})
   * - Tolerances
     - atol=1e-6, rtol=1e-3
     - atol=1e-6, rtol=1e-6 (tighter)
   * - Methods supported
     - RK45, RK23, BDF, Radau, LSODA, DOP853
     - RK45 only (warns for others)
   * - Events
     - Supported
     - Not supported
