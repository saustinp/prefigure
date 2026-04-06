PreFigure C++ Developer Documentation
======================================

PreFigure C++ is a high-performance backend for the `PreFigure <https://prefigure.org>`_
mathematical diagram authoring system. It reimplements the core Python library in C++23
with PyBind11 bindings, providing identical SVG output with significantly faster execution
for batch processing of academic manuscripts and textbooks.

.. note::

   **For diagram authors**: The XML authoring interface is unchanged. Refer to the
   `PreFigure Author's Guide <https://prefigure.org/docs/pr-guide.html>`_ for how to
   write diagrams. This documentation is for **library developers and contributors**.

.. toctree::
   :maxdepth: 2
   :caption: Developer Guide

   guide/index
   guide/building
   guide/architecture
   guide/porting_guide
   guide/expression_eval
   guide/coordinate_transforms
   guide/label_system
   guide/network_layouts
   guide/implicit_curves
   guide/element_handlers
   guide/python_bindings
   guide/testing

.. toctree::
   :maxdepth: 2
   :caption: API Reference

   api/index
   api/types
   api/diagram
   api/parse
   api/expression
   api/ctm
   api/utilities
   api/elements
   api/labels
   api/annotations
   api/diffeqs
   api/network
   api/shapes
   api/axes
