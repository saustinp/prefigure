Label System
============

The label system handles all text rendering in PreFigure diagrams, including
mathematical expressions (via MathJax), plain text (via Cairo), and braille
(via liblouis).  It is the most complex subsystem in terms of external
dependencies and rendering phases.

Two-Phase Pipeline
------------------

Labels are processed in two phases because MathJax requires batch processing
(one Node.js invocation renders all math at once):

**Phase 1: Registration** (during ``Diagram::parse()``)

When the ``label_element()`` handler encounters a ``<label>`` element:

1. Evaluate any ``${expr}`` substitutions in the label text using the
   expression context.
2. Record the label's position, alignment, and font properties.
3. If the label contains ``<m>`` (math) sub-elements, queue the math
   expressions for MathJax.
4. Store the label data in the diagram's label list for later positioning.

**Phase 2: Positioning** (during ``Diagram::place_labels()``)

After all elements are parsed:

1. Invoke MathJax once to render all queued math expressions to SVG.
2. For each label:

   a. Measure the label dimensions (Cairo for text, MathJax output for math).
   b. Compute alignment displacement from the anchor point.
   c. Position the label in the SVG tree.
   d. For tactile output, snap to the embossing grid.

This two-phase approach avoids launching MathJax per-label, providing
significant performance improvement for diagrams with many labels.

MathJax Integration
-------------------

Math rendering is handled by the ``LocalMathLabels`` class in
``label_tools.hpp``.

**Process:**

1. During registration, math expressions are written to a temporary
   JavaScript file alongside a MathJax runner script.
2. When ``place_labels()`` is called, Node.js is invoked once to process
   all expressions.  MathJax renders each to an SVG fragment.
3. The SVG fragments are parsed and their glyph elements integrated into
   the diagram's SVG.

**Glyph ID remapping:**

MathJax generates SVG elements with IDs like ``MJX-1-TEX-I-78``.  Since
multiple labels may use the same glyphs, IDs must be unique across the
document.  The label system prefixes each label's glyph IDs with a unique
counter (e.g., ``LABEL3-MJX-1-TEX-I-78``), and updates all internal
``href`` and ``xlink:href`` references accordingly.

**Dimension extraction:**

MathJax reports dimensions in "ex" units (relative to the font's x-height).
The label system converts these to SVG points using a configurable ex-to-pt
ratio derived from the font size.

Cairo Text Measurement
-----------------------

Plain text (non-math) labels are measured using the ``CairoTextMeasurements``
class, which wraps the Cairo graphics library's font metrics.

**Font face tuples:**

Each label's font is described by a tuple:
``[family, size, italic, bold, color]``.

- ``family``: Font family name (e.g., "serif", "sans-serif")
- ``size``: Font size in points
- ``italic``: Boolean
- ``bold``: Boolean
- ``color``: CSS color string

Cairo's ``text_extents()`` function is called with the appropriate font
configuration to determine the rendered width and height of each text
fragment.

Braille Translation
-------------------

For tactile output, labels are translated to braille using the
``LocalLouisBrailleTranslator`` class, which wraps the liblouis C API.

**Process:**

1. Convert the label text from UTF-8 to wide characters (``wchar_t``).
2. Call ``lou_translateString()`` with the configured translation table.
3. Convert the result back to UTF-8.
4. Measure the braille text using the braille character grid dimensions.

**Embossing grid snapping:**

Braille embossers use a fixed character grid.  The ``snap_to_embossing_grid()``
function rounds label positions to the nearest grid cell to ensure characters
align properly when embossed.  The grid dimensions are:

- Horizontal: 19.2 SVG points per braille cell
- Vertical: 28.8 SVG points per braille line

Alignment Displacement
-----------------------

Labels are positioned relative to an anchor point using a 9-point alignment
system.  The alignment name determines how the label's bounding box is
offset from the anchor:

.. code-block:: text

    northwest ── north ── northeast
        |          |          |
      west ──── center ──── east
        |          |          |
    southwest ── south ── southeast

Each alignment maps to a displacement factor ``(dx, dy)`` where:

- ``dx`` is a fraction of the label width (0 = left edge at anchor,
  -0.5 = centered, -1 = right edge at anchor)
- ``dy`` is a fraction of the label height (0 = top edge at anchor,
  0.5 = centered, 1 = bottom edge at anchor)

For example, ``"southeast"`` uses ``(0, 0)`` — the label's top-left corner
sits at the anchor, so the label extends to the right and below.

The braille displacement table uses different values optimized for the
coarser braille character grid.

Multi-line Layout
-----------------

Labels can contain multiple lines of text (via ``<br/>`` or multiple child
elements).  The layout engine:

1. Measures each line independently.
2. Computes the total height as the sum of line heights plus interline spacing.
3. Positions lines top-to-bottom with configurable justification:

   - ``left`` (default): Lines aligned to left edge
   - ``center``: Lines centered horizontally
   - ``right``: Lines aligned to right edge

The overall label bounding box encompasses all lines, and alignment
displacement is applied to this combined box.
