Implicit Curve Algorithm
========================

The ``implicit.cpp`` module traces level sets of two-variable functions
using an adaptive QuadTree with Newton-Raphson zero-finding.  This guide
explains the algorithm in detail.

Problem Statement
-----------------

Given a function f(x, y) and a level value k, find and render the set of
points where f(x, y) = k within the diagram's bounding box.

Unlike explicit curves y = f(x) which can be sampled directly, implicit
curves require a spatial search to locate where the function crosses zero.

QuadTree Structure
------------------

The algorithm uses a recursive spatial subdivision:

.. code-block:: text

    +--------+--------+
    |   TL   |   TR   |
    |        |        |
    +--------+--------+
    |   BL   |   BR   |
    |        |        |
    +--------+--------+

Each ``QuadTree`` node stores:

- ``corners[4]``: The four corner points (BL, BR, TR, TL order).
- ``depth``: Remaining subdivision levels.

``subdivide()`` splits a cell into four children by computing edge midpoints
and the cell center using the ``midpoint()`` utility.

Algorithm Flow
--------------

**1. Initial uniform subdivision**

The bounding box is subdivided uniformly to ``initial-depth`` levels
(default 4), creating a grid of 4^depth cells.  This ensures the algorithm
doesn't miss thin features that span only a small region.

**2. Zero-crossing detection**

For each leaf cell, ``intersects()`` checks whether f(x,y) - k changes sign
between consecutive corners.  If any adjacent pair of corners has opposite
signs (or either is zero), the zero set passes through this cell.

**3. Adaptive refinement**

Cells that intersect the zero set are subdivided further, down to the maximum
``depth`` (default 8).  Cells that don't intersect are discarded.  This
concentrates resolution where the curve actually is, avoiding wasted
computation in empty regions.

The effective resolution at maximum depth is:

.. math::

   \Delta x = \frac{\text{bbox width}}{2^{\text{depth}}},
   \quad
   \Delta y = \frac{\text{bbox height}}{2^{\text{depth}}}

For depth=8, this gives 256 cells per bbox dimension — sub-pixel accuracy.

**4. Newton-Raphson edge refinement**

On each edge of a leaf cell that has a sign change, ``findzero()`` refines
the crossing location using Newton-Raphson iteration:

1. Start at one corner of the edge.
2. Compute f at the current point.
3. Estimate the derivative df/dt along the edge using a finite difference
   (step size 0.00001 times the edge length).
4. Update: p ← p - f(p) / df.
5. Repeat until |f(p)| < 1e-6 or 50 iterations.

The iteration proceeds along one axis only (whichever the edge is parallel
to), which makes the 1D Newton step simple and robust.

**5. Segment extraction**

Each leaf cell that intersects produces at most one line segment, connecting
two edge-crossing points.  The ``segments()`` method walks around the four
edges in order (TL→BL→BR→TR→TL), finding pairs of crossings and creating
a ``Segment{start, end}`` for each pair.

Edge Cases
----------

- **Corner exactly on the curve**: If a corner value is exactly 0, the
  adjacent edges both register as intersecting.  The segment connects the
  two adjacent zero-crossings normally.

- **Tangent curves**: When the curve touches a cell corner without crossing
  (both adjacent corners have the same sign), ``intersects()`` returns false
  and the cell is not refined.  Very shallow tangencies may be missed.

- **Multiple crossings per cell**: A single cell can only produce one
  segment.  If the curve crosses a cell more than twice (S-curves), increase
  ``initial-depth`` or ``depth`` to ensure cells are small enough that each
  contains at most one crossing.

Performance Considerations
--------------------------

The QuadTree processes cells breadth-first using a work queue.  The number
of leaf cells scales as O(curve length × 2^depth), not O(4^depth), because
adaptive refinement only subdivides cells near the curve.

For most diagrams, the default settings (initial-depth=4, depth=8) provide
good results.  Increase ``initial-depth`` for curves with fine features
that might be missed by the initial uniform grid.
