Porting Guide
=============

This guide explains how to port a Python element handler module from
``prefig/core/`` to C++ in ``prefigure-cpp/src/``. Follow these patterns
to ensure consistency with the existing C++ codebase.

The Handler Function Pattern
----------------------------

Every element handler in Python has this signature::

    def handler(element, diagram, parent, outline_status):
        ...

The C++ equivalent is::

    void handler(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

This function is registered in ``tags.cpp`` via the ``tag_dict`` map, which
maps XML tag names (e.g., ``"graph"``, ``"circle"``) to handler functions.

Step-by-Step Porting Process
----------------------------

1. Read the Python source carefully
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Understand every code path. Pay attention to:

- Which XML attributes are read (``element.get('attr', 'default')``)
- How expressions are evaluated (``un.valid_eval()``)
- Which SVG elements are created (``ET.Element()``, ``ET.SubElement()``)
- How the outline system is used (``add_outline`` / ``finish_outline``)
- Tactile-specific code paths (``diagram.output_format() == 'tactile'``)

2. Read XML attributes
^^^^^^^^^^^^^^^^^^^^^^

Python::

    value = un.valid_eval(element.get('radius', '1'))

C++::

    auto& ctx = diagram.expr_ctx();
    double radius = ctx.eval(
        element.attribute("radius") ? element.attribute("radius").value() : "1"
    ).to_double();

For string attributes that don't need evaluation::

    std::string stroke = element.attribute("stroke")
        ? element.attribute("stroke").value() : "black";

Use the utility helpers where possible::

    std::string val = get_attr(element, "stroke", "black");  // from utilities.hpp

3. Evaluate mathematical expressions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The ``ExpressionContext`` replaces Python's ``un.valid_eval()``::

    // Scalar
    double x = diagram.expr_ctx().eval("sin(pi/4)").to_double();

    // Vector/point
    Point2d p = diagram.expr_ctx().eval("(3, 4)").as_point();

    // Function
    auto f = diagram.expr_ctx().eval("f").as_function();
    double y = f(Value(2.0)).to_double();

    // Define a variable
    diagram.expr_ctx().define("a = 5");

4. Transform coordinates
^^^^^^^^^^^^^^^^^^^^^^^^

Python::

    p_svg = diagram.transform(p)

C++::

    Point2d p_svg = diagram.transform(p);

Always transform user coordinates to SVG coordinates before creating SVG elements.

5. Create SVG elements
^^^^^^^^^^^^^^^^^^^^^^

Python (lxml)::

    path = ET.SubElement(parent, 'path')
    path.set('d', ' '.join(cmds))
    path.set('stroke', 'black')

C++ (pugixml)::

    auto path = parent.append_child("path");
    path.append_attribute("d").set_value(cmd_str.c_str());
    path.append_attribute("stroke").set_value("black");

For creating elements that aren't children of ``parent`` (e.g., markers,
clippath elements), append them to ``diagram.get_defs()``.

6. Apply styling attributes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Python::

    util.set_attr(element, 'stroke', 'black')
    util.set_attr(element, 'thickness', '2')
    util.add_attr(path, util.get_1d_attr(element))

C++::

    set_attr(element, "stroke", "black");
    set_attr(element, "thickness", "2");
    add_attr(path, get_1d_attr(element));

7. Handle the outline system
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Most handlers follow this pattern::

    void handler(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
        // Pass 2: finish outline
        if (status == OutlineStatus::FinishOutline) {
            finish_outline(element, diagram, parent);
            return;
        }

        // ... create SVG element ...

        // Pass 1: add outline (for tactile or explicit outline)
        if (status == OutlineStatus::AddOutline) {
            diagram.add_outline(element, path, parent);
            return;
        }

        // Check if outline is requested
        if (element.attribute("outline") &&
            std::string(element.attribute("outline").value()) == "yes" ||
            diagram.output_format() == OutputFormat::Tactile) {
            diagram.add_outline(element, path, parent);
            finish_outline(element, diagram, parent);
        } else {
            parent.append_child(std::move(path));  // or parent.append_copy(path)
        }
    }

8. Register the handler
^^^^^^^^^^^^^^^^^^^^^^^^

In ``tags.cpp``, add the tag-to-handler mapping::

    dict["my-element"] = my_handler;

Common Pitfalls
---------------

pugixml node lifetime
^^^^^^^^^^^^^^^^^^^^^

Nodes are **non-owning handles**. This is safe::

    auto path = parent.append_child("path");   // path is valid
    path.append_attribute("d").set_value("M 0 0");  // fine

This is **NOT safe**::

    pugi::xml_node make_element() {
        pugi::xml_document doc;
        return doc.append_child("path");  // DANGLING — doc destroyed
    }

Always create nodes as children of a document that outlives them.

Python ``element.text`` vs pugixml
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Python::

    text = element.text  # Text content of element

C++::

    const char* text = element.child_value();  // or element.text().get()

For ``<definition>f(x) = x^2</definition>``, the text ``f(x) = x^2`` is
accessed via ``element.child_value()`` in pugixml.

Python ``copy.deepcopy`` vs pugixml
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Python::

    element_copy = copy.deepcopy(element)

C++ — pugixml doesn't have deep copy, but you can copy nodes between documents::

    pugi::xml_document copy_doc;
    copy_doc.append_copy(element);
    auto element_copy = copy_doc.first_child();

Attribute removal
^^^^^^^^^^^^^^^^^

Python::

    stroke = path.attrib.pop('stroke', 'none')

C++::

    std::string stroke = path.attribute("stroke")
        ? path.attribute("stroke").value() : "none";
    path.remove_attribute("stroke");

Checking attribute existence
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Python::

    if element.get('center', None) is not None:

C++::

    if (element.attribute("center")) {
        // attribute exists
    }

numpy arrays vs Eigen
^^^^^^^^^^^^^^^^^^^^^

Python::

    p = np.array([3.0, 4.0])
    length = np.linalg.norm(p)

C++::

    Point2d p(3.0, 4.0);
    double len = p.norm();  // or length(p) from math_utilities

Python::

    corners = [diagram.transform(c) for c in user_corners]

C++::

    std::vector<Point2d> corners;
    corners.reserve(user_corners.size());
    for (const auto& c : user_corners) {
        corners.push_back(diagram.transform(c));
    }
