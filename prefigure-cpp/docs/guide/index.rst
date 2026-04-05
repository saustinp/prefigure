Developer Guide
===============

This guide is for developers who want to understand, build, modify, or extend
the PreFigure C++ library. If you are a **diagram author** looking to create
mathematical figures, see the `PreFigure Author's Guide <https://prefigure.org/docs/pr-guide.html>`_.

The C++ library is a performance-oriented reimplementation of the Python
``prefig.core`` package. It accepts the same XML input and produces identical
SVG output, but runs significantly faster for batch processing.

.. toctree::
   :maxdepth: 2

   building
   architecture
   porting_guide
   expression_eval
   coordinate_transforms
   python_bindings
   testing
