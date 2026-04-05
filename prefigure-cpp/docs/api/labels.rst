Labels and Text
===============

The label system handles text rendering for mathematical expressions (via
MathJax), plain text (via Cairo), and braille (via liblouis). Labels are
processed in two phases: registration during parsing and positioning after
all diagram elements are rendered.

Label Module
------------

.. doxygenfile:: label.hpp
   :project: prefigure

Label Backend Tools
-------------------

.. doxygenfile:: label_tools.hpp
   :project: prefigure

Legend
------

.. doxygenfile:: legend.hpp
   :project: prefigure
