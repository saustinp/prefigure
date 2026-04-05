Element Handlers
================

Each element handler processes one XML tag type and generates corresponding
SVG output. All handlers share the same function signature::

    void handler(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

Basic Shapes
------------

.. doxygenfile:: line.hpp
   :project: prefigure

.. doxygenfile:: rectangle.hpp
   :project: prefigure

.. doxygenfile:: point.hpp
   :project: prefigure

.. doxygenfile:: circle.hpp
   :project: prefigure

.. doxygenfile:: polygon.hpp
   :project: prefigure

Curves and Graphs
-----------------

.. doxygenfile:: graph.hpp
   :project: prefigure

.. doxygenfile:: parametric_curve.hpp
   :project: prefigure

.. doxygenfile:: tangent_line.hpp
   :project: prefigure

.. doxygenfile:: implicit.hpp
   :project: prefigure

Regions and Areas
-----------------

.. doxygenfile:: area.hpp
   :project: prefigure

.. doxygenfile:: riemann_sum.hpp
   :project: prefigure

Fields
------

.. doxygenfile:: slope_field.hpp
   :project: prefigure

Arrows and Vectors
------------------

.. doxygenfile:: arrow.hpp
   :project: prefigure

.. doxygenfile:: vector_element.hpp
   :project: prefigure

Paths
-----

.. doxygenfile:: path_element.hpp
   :project: prefigure

Structure and Transforms
------------------------

.. doxygenfile:: coordinates.hpp
   :project: prefigure

.. doxygenfile:: clip.hpp
   :project: prefigure

.. doxygenfile:: group.hpp
   :project: prefigure

.. doxygenfile:: repeat.hpp
   :project: prefigure

.. doxygenfile:: definition.hpp
   :project: prefigure

Data and Images
---------------

.. doxygenfile:: statistics.hpp
   :project: prefigure

.. doxygenfile:: image.hpp
   :project: prefigure

.. doxygenfile:: read.hpp
   :project: prefigure
