Coordinate Transforms
=====================

The CTM (Current Transformation Matrix) class and affine matrix utility
functions. See also the :doc:`../guide/coordinate_transforms` narrative guide.

CTM Class
---------

.. doxygenclass:: prefigure::CTM
   :project: prefigure
   :members:
   :undoc-members:

Affine Matrix Functions
-----------------------

.. doxygenfunction:: prefigure::affine_identity
   :project: prefigure

.. doxygenfunction:: prefigure::affine_translation
   :project: prefigure

.. doxygenfunction:: prefigure::affine_scaling
   :project: prefigure

.. doxygenfunction:: prefigure::affine_rotation
   :project: prefigure

.. doxygenfunction:: prefigure::affine_concat
   :project: prefigure

SVG Transform String Generators
--------------------------------

.. doxygenfunction:: prefigure::translatestr
   :project: prefigure

.. doxygenfunction:: prefigure::scalestr
   :project: prefigure

.. doxygenfunction:: prefigure::rotatestr
   :project: prefigure

Transform Element Handlers
--------------------------

.. doxygenfunction:: prefigure::transform_group
   :project: prefigure

.. doxygenfunction:: prefigure::transform_translate
   :project: prefigure

.. doxygenfunction:: prefigure::transform_rotate
   :project: prefigure

.. doxygenfunction:: prefigure::transform_scale
   :project: prefigure
