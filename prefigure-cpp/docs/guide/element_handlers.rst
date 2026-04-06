Element Handler Guide
=====================

This guide explains how element handlers work, common patterns shared
across handlers, and how to add a new one.

Handler Function Signature
--------------------------

Every element handler has the same signature::

    void handler(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

- ``element``: The source XML element being processed (e.g., ``<circle>``).
  Handlers may read and modify its attributes.
- ``diagram``: The central orchestrator providing coordinate transforms,
  expression evaluation, SVG tree access, and registration APIs.
- ``parent``: The SVG node to append output elements to.
- ``status``: The outline rendering pass (see below).

Handlers are registered in ``tags.cpp`` via the tag dictionary::

    dict["circle"] = circle_element;
    dict["axes"] = axes;
    dict["tangent-line"] = tangent;

The dispatcher ``parse_element()`` looks up the element's tag name and
calls the corresponding handler.

Outline Two-Pass System
-----------------------

For tactile diagrams and elements with ``@outline="yes"``, rendering
happens in two passes to ensure foreground elements are legible against
complex backgrounds:

**Pass 1: AddOutline**

The handler creates the SVG element normally, but instead of appending it
to the parent, stores it in ``<defs>`` as a reusable via
``diagram.add_outline(element, svg_node, parent)``.  A thick white stroke
version is drawn behind the element.

**Pass 2: FinishOutline**

The handler is called again with ``OutlineStatus::FinishOutline``.  It
calls ``diagram.finish_outline()`` which creates a ``<use>`` reference to
the stored path, drawing the actual element on top of the white outline.

**Standard pattern**::

    void my_handler(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
        if (status == OutlineStatus::FinishOutline) {
            diagram.finish_outline(element,
                element.attribute("stroke").value(),
                element.attribute("thickness").value(),
                get_attr(element, "fill", "none"),
                parent);
            return;
        }

        // ... create SVG element ...

        if (status == OutlineStatus::AddOutline) {
            diagram.add_outline(element, svg_node, parent);
            return;
        }

        // Normal rendering (or auto-outline for tactile)
        if (get_attr(element, "outline", "no") == "yes" ||
            diagram.output_format() == OutputFormat::Tactile) {
            diagram.add_outline(element, svg_node, parent);
            diagram.finish_outline(element, ...);
        } else {
            parent.append_copy(svg_node);
        }
    }

Coordinate Transform Flow
--------------------------

User coordinates (from XML) must be transformed to SVG coordinates before
rendering:

1. **Read user coordinates**: Parse from XML attributes using
   ``diagram.expr_ctx().eval()``.
2. **Transform to SVG**: Call ``diagram.transform(point)`` which applies
   the current CTM (coordinate transform matrix) including any active
   coordinate system nesting.
3. **Write SVG coordinates**: Use ``pt2str()`` or ``float2str()`` to
   format for SVG attributes.

For inverse operations (SVG → user), use ``diagram.inverse_transform()``.

Common Patterns
---------------

**Reading attributes with defaults**::

    std::string stroke = get_attr(element, "stroke", "black");
    set_attr(element, "thickness", "2");  // set default if not present

**Tactile mode overrides**::

    if (diagram.output_format() == OutputFormat::Tactile) {
        element.attribute("stroke").set_value("black");
        element.attribute("fill").set_value("lightgray");
    }

**Applying styling to SVG elements**::

    add_attr(path, get_1d_attr(element));  // stroke, thickness (for curves)
    add_attr(path, get_2d_attr(element));  // stroke, fill, thickness (for filled shapes)

**Clipping to bounding box**::

    if (!element.attribute("cliptobbox")) {
        element.append_attribute("cliptobbox").set_value("yes");
    }
    cliptobbox(svg_node, element, diagram);

**Adding arrowheads**::

    int arrows = std::stoi(get_attr(element, "arrows", "0"));
    if (arrows > 0) add_arrowhead_to_path(diagram, "marker-end", path);
    if (arrows > 1) add_arrowhead_to_path(diagram, "marker-start", path);

**Registering with the diagram**::

    diagram.add_id(svg_node, get_attr(element, "id", ""));
    diagram.register_svg_element(element, svg_node);

**Creating SVG elements in scratch space**::

    XmlNode path = diagram.get_scratch().append_child("path");
    // ... set attributes ...
    parent.append_copy(path);  // copy from scratch to final tree

Adding a New Element Handler
-----------------------------

1. **Create header** ``include/prefigure/my_element.hpp`` with the handler
   declaration and Doxygen documentation.

2. **Create source** ``src/my_element.cpp`` implementing the handler.  Follow
   the outline pattern above.

3. **Register** in ``src/tags.cpp``::

       #include "prefigure/my_element.hpp"
       // In get_tag_dict():
       dict["my-element"] = my_element;

4. **Add to build** in ``CMakeLists.txt``::

       set(PREFIGURE_CORE_SOURCES
           ...
           src/my_element.cpp
       )

5. **Add tests** in ``tests/`` with a Catch2 test case and an example XML
   file in ``tests/resources/``.

6. **Document** the new element in the appropriate API reference page
   (``docs/api/elements.rst``).
