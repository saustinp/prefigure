Coordinate Transforms
=====================

The coordinate transformation system converts between the user's mathematical
coordinate system and SVG pixel coordinates. It is managed by the ``CTM``
(Current Transformation Matrix) class and supports nested coordinate systems
with optional logarithmic scaling.

Mathematical Model
------------------

The CTM is a 2x3 affine transformation matrix using homogeneous coordinates:

.. math::

   \begin{pmatrix} x' \\ y' \end{pmatrix} =
   \begin{pmatrix} a & b & c \\ d & e & f \end{pmatrix}
   \begin{pmatrix} x \\ y \\ 1 \end{pmatrix}

Where :math:`(x, y)` are user coordinates and :math:`(x', y')` are SVG
coordinates. The matrix encodes translation, rotation, scaling, and reflection.

The CTM Stack
-------------

Coordinate systems can be nested via ``<coordinates>`` XML elements. The
``Diagram`` maintains a stack of ``(CTM, BBox)`` pairs:

- ``push_ctm(ctm, bbox)`` — enter a nested coordinate system
- ``pop_ctm()`` — restore the previous coordinate system
- ``ctm()`` — access the current CTM
- ``bbox()`` — access the current bounding box

The initial CTM (set in ``begin_figure()``) handles the SVG y-axis flip
(SVG has y increasing downward; mathematical coordinates have y increasing
upward) and margin offsets.

**SVG coordinate setup** (non-tactile)::

    // CTM maps user bbox [0,0,width,height] to SVG canvas
    ctm.translate(0, height + margin_top + margin_bottom);
    ctm.scale(1, -1);  // flip y-axis
    ctm.translate(margin_left, margin_bottom);

**Tactile coordinate setup**::

    // Scale to fit 11.5" x 11" paper (828 x 792 SVG points)
    ctm.translate(36, lly);  // 0.5" margin
    ctm.scale(s, -s);        // uniform scale + y-flip
    ctm.translate(margin_left, margin_bottom);

Nested Coordinate Systems
-------------------------

The ``<coordinates>`` XML element creates a new coordinate system:

1. Parse ``@bbox`` — the new user coordinate range
2. Parse ``@destination`` — the region in the parent system to map into
3. Compute the CTM that maps bbox to destination
4. Create a clippath to clip rendering to the destination region
5. Push the new CTM and parse children
6. Pop the CTM when done

The CTM composition is::

    ctm = parent_ctm.copy()
    ctm.translate(dest_x0, dest_y0)
    ctm.scale(dest_width / bbox_width, dest_height / bbox_height)
    ctm.translate(-bbox_x0, -bbox_y0)

Logarithmic Scales
------------------

The ``@scales`` attribute on ``<coordinates>`` supports:

- ``linear`` — both axes linear (default)
- ``semilogx`` — x-axis logarithmic, y-axis linear
- ``semilogy`` — x-axis linear, y-axis logarithmic
- ``loglog`` — both axes logarithmic

When log scaling is enabled:

1. The bbox coordinates are converted: :math:`x \to \log_{10}(x)`
2. The CTM's ``scale_x_`` / ``scale_y_`` lambdas are set to ``log10()``
3. Points are log-transformed before the affine matrix is applied
4. The inverse uses ``10^x`` to reverse

Aspect Ratio
-------------

The ``@aspect-ratio`` attribute constrains the coordinate system so that
one unit in x equals ``ratio`` units in y visually. Combined with
``@preserve-y-range``, it controls which axis is adjusted:

- ``preserve-y-range="yes"``: keeps y-range fixed, adjusts x-range
- Otherwise: keeps x-range fixed, adjusts y-range

CTM Operations
--------------

The ``CTM`` class supports these transformations (each updates both the
forward matrix and its inverse):

- ``translate(x, y)`` — shift the origin
- ``scale(sx, sy)`` — scale axes (can be negative for reflection)
- ``rotate(theta, units)`` — rotate by angle (degrees or radians)
- ``apply_matrix(m00, m01, m10, m11)`` — apply arbitrary 2x2 matrix

All operations compose via matrix multiplication in the order they're called.
The inverse is maintained incrementally (not recomputed from scratch).

SVG Transform Strings
----------------------

For non-tactile output, group transforms are applied as SVG ``transform``
attributes rather than modifying the CTM. Helper functions generate the
SVG syntax:

- ``translatestr(x, y)`` → ``"translate(10.0,20.0)"``
- ``scalestr(sx, sy)`` → ``"scale(2,3)"``
- ``rotatestr(theta)`` → ``"rotate(-45.0)"`` (negated for SVG convention)
- ``matrixstr(m)`` → ``"matrix(a,b,c,d,e,f)"``
