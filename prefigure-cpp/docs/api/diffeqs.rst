ODE Solver
==========

The ODE solver module provides numerical integration of ordinary differential
equations using a custom Dormand-Prince RK45 adaptive stepper.  The solution
is stored in the expression namespace for subsequent plotting.

See :doc:`../guide/ode_solver` for algorithm details including step size
control, dense output interpolation, and discontinuity handling.

.. doxygenfile:: diffeqs.hpp
   :project: prefigure
