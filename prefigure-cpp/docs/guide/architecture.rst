Architecture
============

This document describes the internal architecture of the PreFigure C++ library,
including module dependencies, data flow, and key design decisions.

Module Overview
---------------

The library is organized into four layers:

1. **Entry points** — ``parse.cpp`` loads XML and orchestrates the pipeline
2. **Core infrastructure** — ``Diagram``, ``CTM``, ``ExpressionContext`` manage state
3. **Element handlers** — 30+ modules that each render one XML element type to SVG
4. **Utilities** — math, string formatting, SVG helpers shared across handlers

.. code-block:: text

    XML Input
        |
        v
    parse.cpp  ──>  mk_diagram()
        |
        v
    Diagram::begin_figure()     Set up SVG canvas, CTM, clippath
        |
        v
    Diagram::parse()            For each child element:
        |                         1. Strip namespace
        |                         2. Apply publication defaults
        |                         3. Apply format-specific overrides
        |                         4. Dispatch to tags::parse_element()
        |                                |
        |                                v
        |                         tag_dict[tag] ──> handler(element, diagram, parent, status)
        |                                              |
        |                                              v
        |                                         Create SVG elements
        |                                         Transform coordinates
        |                                         Register with diagram
        v
    Diagram::place_labels()     Batch-render math via MathJax, position all labels
        |
        v
    Diagram::annotate_source()  Generate accessibility annotations
        |
        v
    Diagram::end_figure()       Write SVG and annotation XML to files
        |
        v
    SVG Output + Annotations XML

Python-to-C++ Module Mapping
-----------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 30 40

   * - Python Module
     - C++ File
     - Key Classes/Functions
   * - ``diagram.py``
     - ``diagram.cpp``
     - ``Diagram`` class
   * - ``parse.py``
     - ``parse.cpp``
     - ``parse()``, ``mk_diagram()``
   * - ``tags.py``
     - ``tags.cpp``
     - ``parse_element()``, ``get_tag_dict()``
   * - ``user_namespace.py``
     - ``user_namespace.cpp``
     - ``ExpressionContext`` class
   * - ``CTM.py``
     - ``ctm.cpp``
     - ``CTM`` class, affine functions
   * - ``utilities.py``
     - ``utilities.cpp``
     - ``get_attr()``, ``float2str()``, etc.
   * - ``math_utilities.py``
     - ``math_utilities.cpp``
     - ``dot()``, ``normalize()``, etc.
   * - ``graph.py``
     - ``graph.cpp``
     - ``graph()``, ``cartesian_path()``
   * - ``circle.py``
     - ``circle.cpp``
     - ``circle_element()``, ``arc()``, ``angle_marker()``
   * - ``label.py``
     - ``label.cpp``
     - ``label()``, ``place_labels()``
   * - (etc.)
     - (etc.)
     - (etc.)

Key Design Decisions
--------------------

Expression Context is Instance-Based
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Python's ``user_namespace.py`` uses module-level globals reset via
``importlib.reload()``. The C++ port uses an ``ExpressionContext`` instance
owned by each ``Diagram``. This eliminates global mutable state and makes
the library thread-safe for concurrent diagram generation.

Access it via ``diagram.expr_ctx()`` from any element handler.

pugixml Node Ownership
^^^^^^^^^^^^^^^^^^^^^^

Unlike lxml where ``ET.Element('tag')`` creates a standalone element, pugixml
nodes are non-owning handles — they're valid only while their parent
``xml_document`` exists. The ``Diagram`` class owns ``svg_doc_`` which holds
all SVG nodes for the diagram's lifetime.

**Implications:**

- Never store an ``XmlNode`` that outlives its document
- Create elements as children of ``svg_doc_`` (via ``get_root()`` or ``get_defs()``)
- Use ``scratch_doc_`` for temporary elements (annotations, templates)

No .tail Concept
^^^^^^^^^^^^^^^^

lxml elements have ``.text`` and ``.tail`` properties. pugixml has ``text()``
(first child text node) but no tail. For mixed content like::

    <label>Hello <m>x^2</m> world</label>

The text "world" after ``<m>`` is a separate ``pugi::node_pcdata`` sibling,
not a property of the ``<m>`` element. Iterate all child nodes (including
PCDATA) to handle mixed content.

Outline Two-Pass Rendering
^^^^^^^^^^^^^^^^^^^^^^^^^^

For tactile diagrams and explicit outline requests, elements are rendered twice:

1. **Add outline pass** (``OutlineStatus::AddOutline``): Draws a thick white
   stroke behind the element to create visual separation from background content.
   The element's SVG path is stored in ``<defs>`` as a reusable.

2. **Finish outline pass** (``OutlineStatus::FinishOutline``): Draws the actual
   element on top of the outline using a ``<use>`` reference to the reusable path.

This ensures foreground elements are always legible against complex backgrounds.

Label Two-Phase System
^^^^^^^^^^^^^^^^^^^^^^

Labels are processed in two phases because MathJax rendering requires batch
processing (one Node.js invocation for all math expressions):

1. **Registration phase** (during ``Diagram::parse()``): The ``label()`` handler
   records each label's position, alignment, and math content. Math expressions
   are queued for MathJax.

2. **Positioning phase** (during ``Diagram::place_labels()``): MathJax is invoked
   once to render all math. Then each label is measured (via Cairo for text,
   MathJax output for math) and positioned in the SVG.

Library Dependency Map
----------------------

.. code-block:: text

    Required:
      Eigen 3.4+  ──>  Point2d, vectors, matrices
      pugixml     ──>  XML parsing and SVG generation
      spdlog      ──>  Logging (debug, info, warning, error)
      exprtk      ──>  Mathematical expression evaluation

    Optional:
      Boost.Odeint    ──>  ODE solving (diffeqs.cpp)
      Boost.Graph     ──>  Network graph layouts (network.cpp)
      GEOS            ──>  Boolean geometry operations (shape.cpp)
      libcairo        ──>  Text measurement for labels (label_tools.cpp)
      liblouis        ──>  Braille translation for tactile (label_tools.cpp)
      Node.js/MathJax ──>  Mathematical label rendering (label_tools.cpp)
      pybind11        ──>  Python bindings (bindings/)
