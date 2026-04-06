#pragma once

#include "types.hpp"
#include "ctm.hpp"
#include "user_namespace.hpp"

#include <array>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace prefigure {

/**
 * @brief Central orchestrator for building a single SVG diagram from XML input.
 *
 * A Diagram owns the SVG document tree, the expression evaluation context,
 * the coordinate-transform stack, and all bookkeeping (IDs, clip paths,
 * annotations, labels, shapes, etc.).
 *
 * @par Lifecycle
 * 1. Construct with the source `<diagram>` XML element and configuration.
 * 2. Call begin_figure() to set up dimensions, margins, CTM, and the SVG root.
 * 3. Call parse() to walk child elements and dispatch to tag handlers.
 * 4. Call place_labels() to position any deferred MathJax/tactile labels.
 * 5. Call end_figure() to write SVG (and annotations) to disk, or
 *    end_figure_to_string() to obtain the SVG as a string.
 *
 * @note A Diagram is not copyable because it owns XML document trees.
 *
 * @see parse.hpp for the top-level entry points that create Diagram objects.
 * @see ExpressionContext for the math evaluation engine.
 */
class Diagram {
public:
    /**
     * @brief Construct a Diagram from an XML `<diagram>` element.
     *
     * Sets up the SVG root, reads publication defaults, processes templates,
     * and initializes the expression context with built-in math functions.
     *
     * @param diagram_element  The `<diagram>` XML node from the source file.
     * @param filename         Path to the source XML file (used for output naming).
     * @param diagram_number   Index when multiple diagrams exist in one file; nullopt for single.
     * @param format           SVG or Tactile output mode.
     * @param output           Optional explicit output path (reserved for future use).
     * @param publication      The `<prefigure>` node from the publication file, or null.
     * @param suppress_caption If true, do not emit a caption element.
     * @param environment      The host environment (PreTeXt, CLI, or Pyodide).
     */
    Diagram(XmlNode diagram_element, const std::string& filename,
            std::optional<int> diagram_number, OutputFormat format,
            std::optional<std::string> output, XmlNode publication,
            bool suppress_caption, Environment environment);

    // -- Figure lifecycle ---------------------------------------------------

    /**
     * @brief Initialize the SVG document: compute dimensions, margins, initial CTM, and clip path.
     *
     * Must be called exactly once before parse().
     * Reads `dimensions`, `width`/`height`, `margins`, and `tactile-margins`
     * attributes from the source `<diagram>` element.
     */
    void begin_figure();

    /**
     * @brief Walk the XML children of @p element and dispatch each to its tag handler.
     *
     * @param element        The XML parent whose children will be processed (defaults to diagram root).
     * @param root           The SVG parent node for appending output (defaults to SVG root).
     * @param outline_status Current outline rendering pass.
     *
     * @details For each child element, this method:
     *   - Strips namespace prefixes
     *   - Converts `@at` to `@id`
     *   - Validates EPUB-safe IDs
     *   - Merges publication-file defaults
     *   - Replaces format-specific attributes (e.g., `tactile-stroke`)
     *   - Dispatches to the registered tag handler via tags::parse_element()
     */
    void parse(std::optional<XmlNode> element = std::nullopt,
               std::optional<XmlNode> root = std::nullopt,
               OutlineStatus outline_status = OutlineStatus::None);

    /**
     * @brief Position all deferred labels (MathJax or tactile) that were queued during parse().
     *
     */
    void place_labels();

    /**
     * @brief Finalize the diagram and write SVG, diagcess-SVG, and annotations to disk.
     *
     * Output files are written to an `output/` subdirectory next to the source file.
     * File names are derived from the source filename and optional diagram_number.
     */
    void end_figure();

    /**
     * @brief Finalize the diagram and return the SVG (and optional annotations) as strings.
     *
     * @return A pair of (SVG string, optional annotation-XML string).
     */
    std::pair<std::string, std::optional<std::string>> end_figure_to_string();

    /**
     * @brief Write annotation metadata to the source XML tree.
     *
     * @note Currently a stub.
     */
    void annotate_source();

    // -- Coordinate transform stack -----------------------------------------

    /**
     * @brief Get a mutable reference to the current (top-of-stack) CTM.
     * @return Reference to the active Current Transformation Matrix.
     */
    CTM& ctm();

    /**
     * @brief Get the current bounding box (in user coordinates).
     * @return The active BBox [x_min, y_min, x_max, y_max].
     */
    BBox bbox();

    /**
     * @brief Get the current CTM and bounding box as a pair.
     * @return (CTM, BBox) pair from the top of the stack.
     */
    std::pair<CTM, BBox> ctm_bbox();

    /**
     * @brief Push a new CTM/BBox pair onto the transform stack.
     * @param ctm_bbox The CTM and bounding box to push.
     * @see pop_ctm()
     */
    void push_ctm(std::pair<CTM, BBox> ctm_bbox);

    /**
     * @brief Pop and return the top CTM/BBox pair from the transform stack.
     * @return The removed (CTM, BBox) pair.
     * @throws std::runtime_error if the stack is empty.
     * @see push_ctm()
     */
    std::pair<CTM, BBox> pop_ctm();

    /**
     * @brief Transform a point from user coordinates to SVG coordinates.
     * @param p Point in user (mathematical) coordinates.
     * @return Corresponding point in SVG pixel coordinates.
     */
    Point2d transform(const Point2d& p);

    /**
     * @brief Transform a point from SVG coordinates back to user coordinates.
     * @param p Point in SVG pixel coordinates.
     * @return Corresponding point in user (mathematical) coordinates.
     */
    Point2d inverse_transform(const Point2d& p);

    // -- ID management ------------------------------------------------------

    /**
     * @brief Generate a unique ID and set it as the "id" attribute on @p element.
     *
     * If @p id is empty, an auto-generated ID based on the element's tag name is used.
     * The current ID suffix and prefix are applied.
     *
     * @param element The XML/SVG node to receive the ID attribute.
     * @param id      Optional explicit ID base string.
     *
     * @see find_id(), prepend_id_prefix()
     */
    void add_id(XmlNode element, const std::string& id = "");

    /**
     * @brief Compute a unique ID string without setting it on any element.
     *
     * @param element Used for tag-name-based auto-generation when @p id is empty.
     * @param id      Optional explicit ID base string.
     * @return The fully-qualified ID string (with prefix and suffix applied).
     */
    std::string find_id(XmlNode element, const std::string& id = "");

    /**
     * @brief Compute an ID for @p element by appending the current suffix.
     * @param element The element whose existing `@id` (if any) is used as the base.
     * @return The ID string with suffix appended.
     */
    std::string append_id_suffix(XmlNode element);

    /**
     * @brief Prepend the diagram's ID prefix to the given ID, if prefixing is enabled.
     * @param id The raw ID string.
     * @return The prefixed ID, or the original if prefixing is disabled or already applied.
     */
    std::string prepend_id_prefix(const std::string& id);

    // -- Clip path stack ----------------------------------------------------

    /**
     * @brief Push a new clip path onto the clip-path stack and register it in `<defs>`.
     * @param clippath An XML `<clipPath>` element to register.
     */
    void push_clippath(XmlNode clippath);

    /**
     * @brief Pop the most recent clip path from the stack.
     */
    void pop_clippath();

    /**
     * @brief Get the ID of the current (innermost) clip path.
     * @return The clip-path ID string, or empty if no clip path is active.
     */
    std::string get_clippath();

    // -- Axis scale stack ---------------------------------------------------

    /**
     * @brief Push axis scale types onto the scale stack.
     * @param scales A pair of scale names, e.g., {"linear", "log"} for [x, y].
     */
    void push_scales(std::array<std::string, 2> scales);

    /**
     * @brief Pop the most recent axis scales from the stack.
     */
    void pop_scales();

    /**
     * @brief Get the current axis scale types.
     * @return A pair of scale names [x_scale, y_scale], defaulting to {"linear", "linear"}.
     */
    std::array<std::string, 2> get_scales();

    // -- SVG tree access ----------------------------------------------------

    /**
     * @brief Get the root `<svg>` element of the output document.
     * @return The SVG root node.
     */
    XmlNode get_root();

    /**
     * @brief Get the `<defs>` element for storing reusable SVG definitions.
     * @return The `<defs>` node.
     */
    XmlNode get_defs();

    /**
     * @brief Get a scratch node for creating temporary SVG elements.
     *
     * Elements created as children of this node persist for the lifetime
     * of the Diagram. Use this when you need to build an element in memory
     * before copying it into the SVG tree.
     *
     * @return A persistent XML node owned by the scratch document.
     */
    XmlNode get_scratch();

    /**
     * @brief Register a reusable SVG element (marker, clipPath, etc.) in `<defs>`.
     *
     * The element is copied into `<defs>` and indexed by its `@id`.
     * Duplicate IDs are silently ignored.
     *
     * @param element The SVG element to register.
     */
    void add_reusable(XmlNode element);

    /**
     * @brief Check whether a reusable element with the given ID has been registered.
     * @param id The element ID to look up.
     * @return True if the ID exists in the reusables dictionary.
     */
    bool has_reusable(const std::string& id);

    /**
     * @brief Retrieve a previously registered reusable element by ID.
     * @param id The element ID.
     * @return The XML node, or a null node if not found.
     */
    XmlNode get_reusable(const std::string& id);

    // -- Configuration queries ----------------------------------------------

    /**
     * @brief Get the current output format (SVG or Tactile).
     * @return The OutputFormat.
     */
    OutputFormat output_format() const;

    /**
     * @brief Set the output format (used temporarily during shape definitions).
     * @param fmt The new OutputFormat.
     */
    void set_output_format(OutputFormat fmt);

    /**
     * @brief Get the host environment.
     * @return The Environment enum value.
     */
    Environment get_environment() const;

    // -- SVG element registration and labels --------------------------------

    /**
     * @brief Link a source XML element to its generated SVG counterpart.
     *
     * Used by the playground's source-annotation feature to trace SVG output
     * back to input XML.
     *
     * @param source    The original XML element.
     * @param svg       The generated SVG element.
     * @param overwrite If true (default), replace any existing mapping.
     */
    void register_svg_element(XmlNode source, XmlNode svg, bool overwrite = true);

    /**
     * @brief Register a label for deferred placement.
     *
     * The label group and the CTM at the time of registration are stored
     * so that place_labels() can position it later.
     *
     * @param element The source XML element that owns the label.
     * @param group   The SVG `<g>` node containing the label content.
     */
    void add_label(XmlNode element, XmlNode group);

    /**
     * @brief Store the rendered dimensions of a label for later positioning.
     * @param element The source XML element owning the label.
     * @param dims    (width, height) in SVG pixels.
     */
    void register_label_dims(XmlNode element, std::pair<double, double> dims);

    /**
     * @brief Retrieve the rendered dimensions of a previously registered label.
     * @param element The source XML element owning the label.
     * @return (width, height) in SVG pixels, or (0, 0) if not found.
     */
    std::pair<double, double> get_label_dims(XmlNode element);

    /**
     * @brief Register a legend for deferred placement.
     * @param legend Pointer to the Legend object (diagram does NOT own it).
     */
    void add_legend(void* legend);

    /**
     * @brief Retrieve the label group and CTM for a given source element.
     * @param element The source XML element.
     * @return (group, CTM) pair, or null group if not found.
     */
    std::tuple<XmlNode, XmlNode, CTM> get_label_group(XmlNode element);

    /**
     * @brief Get a mutable reference to the label group dictionary.
     * @return Reference to the map keyed by element hash_value.
     */
    std::unordered_map<size_t, std::tuple<XmlNode, XmlNode, CTM>>& get_label_group_dict();

    /**
     * @brief Get the source-to-SVG element mapping.
     */
    const std::unordered_map<size_t, XmlNode>& get_source_to_svg_map() const;

    // -- Shape dictionary ---------------------------------------------------

    /**
     * @brief Register a `<shape>` SVG node for later clipping or referencing.
     * @param shape_node The SVG node (typically a `<path>`) to store.
     */
    void add_shape(XmlNode shape_node);

    /**
     * @brief Look up a shape by ID in the shape dictionary only.
     * @param id The shape ID.
     * @return The shape node, or a null node if not found.
     */
    XmlNode recall_shape(const std::string& id);

    /**
     * @brief Look up a shape by ID, falling back to searching the SVG root for `<path>` elements.
     * @param id The shape ID.
     * @return The shape node, or a null node (with an error logged) if not found.
     */
    XmlNode get_shape(const std::string& id);

    /**
     * @brief Apply publication-file default attributes to an element.
     *
     * Any attributes defined in the publication defaults for @p tag that are
     * not already present on @p element are copied over.
     *
     * @param tag     The XML tag name to look up in defaults.
     * @param element The element to receive default attributes.
     */
    void apply_defaults(const std::string& tag, XmlNode element);

    /**
     * @brief Get the external data directory root from the publication file.
     * @return The directory path string, or empty if not configured.
     */
    std::string get_external();

    /**
     * @brief Get the diagram margins [left, bottom, right, top].
     * @return The four margin values in user-coordinate units.
     */
    std::array<double, 4> get_margins();

    // -- Outline rendering --------------------------------------------------

    /**
     * @brief Create a white background outline for a path (first pass of two-pass outline).
     *
     * Extracts stroke/fill from @p path, registers the path as a reusable `<defs>` element,
     * and appends a `<use>` with a widened white stroke.
     *
     * @param element       The source XML element (for ID generation).
     * @param path          The SVG path element to outline.
     * @param parent        The SVG parent node to append the `<use>` to.
     * @param outline_width Extra width added to each side of the stroke
     *                      (default: 18 for tactile, 4 for SVG).
     *
     * @see finish_outline()
     */
    void add_outline(XmlNode element, XmlNode path, XmlNode parent, int outline_width = -1);

    /**
     * @brief Draw the colored foreground stroke over a previously created outline (second pass).
     *
     * @param element   The source XML element.
     * @param stroke    Stroke color string.
     * @param thickness Stroke width string.
     * @param fill      Fill color string.
     * @param parent    The SVG parent node to append the `<use>` to.
     *
     * @see add_outline()
     */
    void finish_outline(XmlNode element, const std::string& stroke,
                       const std::string& thickness, const std::string& fill, XmlNode parent);

    // -- Annotations --------------------------------------------------------

    /**
     * @brief Set up the annotation root node for this diagram.
     */
    void initialize_annotations();

    /**
     * @brief Add an annotation node to the annotation tree.
     * @param annotation The `<annotation>` XML node.
     */
    void add_annotation(XmlNode annotation);

    /**
     * @brief Add an annotation to the default-annotations list.
     * @param annotation The `<annotation>` XML node.
     */
    void add_default_annotation(XmlNode annotation);

    /**
     * @brief Get a mutable reference to the default annotations list.
     * @return Vector of default annotation nodes.
     */
    std::vector<XmlNode>& get_default_annotations();

    /**
     * @brief Get the root node of the annotations tree.
     * @return The annotations root XML node.
     */
    XmlNode get_annotations_root();

    /**
     * @brief Push a node onto the annotation branch stack (for nested annotation scoping).
     * @param annotation The annotation branch node.
     */
    void push_to_annotation_branch(XmlNode annotation);

    /**
     * @brief Pop the most recent annotation branch from the stack.
     */
    void pop_from_annotation_branch();

    /**
     * @brief Append an annotation to the current annotation branch.
     * @param annotation The `<annotation>` node to append.
     */
    void add_annotation_to_branch(XmlNode annotation);

    /**
     * @brief Retrieve a named annotation branch.
     * @param id The branch identifier.
     * @return The annotation branch node.
     */
    XmlNode get_annotation_branch(const std::string& id);

    // -- ID suffix management -----------------------------------------------

    /**
     * @brief Push an ID suffix for the current scope (used by `<repeat>`).
     *
     * While active, all generated IDs will have this suffix appended,
     * ensuring uniqueness across repeat iterations.
     *
     * @param suffix The suffix string (typically "-0", "-1", etc.).
     */
    void push_id_suffix(const std::string& suffix);

    /**
     * @brief Pop the most recent ID suffix.
     */
    void pop_id_suffix();

    // -- Data storage -------------------------------------------------------

    /**
     * @brief Store a keyed data value associated with a source XML element.
     *
     * Used by element handlers to save computed data (e.g., endpoint coordinates)
     * for later retrieval by labels or annotations.
     *
     * @param element The source XML element (keyed by hash_value).
     * @param key     A string key for the data item.
     * @param value   The value to store.
     */
    void register_source_data(XmlNode element, const std::string& key, const Value& value);

    /**
     * @brief Retrieve a keyed data value associated with a source XML element.
     * @param element The source XML element.
     * @param key     The string key.
     * @return The stored Value, or an empty Value if not found.
     */
    Value get_source_data(XmlNode element, const std::string& key);

    /**
     * @brief Store a single Value associated with a source element (unkeyed).
     * @param element The source XML element.
     * @param data    The value to store.
     */
    void save_data(XmlNode element, const Value& data);

    /**
     * @brief Retrieve the unkeyed Value stored for a source element.
     * @param element The source XML element.
     * @return The stored Value, or an empty Value if not found.
     */
    Value retrieve_data(XmlNode element);

    // -- Network data -------------------------------------------------------

    /**
     * @brief Store computed node coordinates for a named network.
     * @param network_name The network identifier.
     * @param coordinates  A Value (typically a vector) of node positions.
     */
    void save_network_coordinates(const std::string& network_name, const Value& coordinates);

    /**
     * @brief Retrieve stored node coordinates for a named network.
     * @param network_name The network identifier.
     * @return The stored coordinates Value, or empty if not found.
     */
    Value get_network_coordinates(const std::string& network_name);

    // -- Expression context -------------------------------------------------

    /**
     * @brief Get a mutable reference to this diagram's expression evaluation context.
     * @return Reference to the ExpressionContext.
     */
    ExpressionContext& expr_ctx();

    /**
     * @brief Get the source diagram XML element.
     * @return The diagram XML node.
     */
    XmlNode get_diagram_element() const;

    // -- Caption ------------------------------------------------------------

    /**
     * @brief Set the diagram caption text.
     * @param text The caption string.
     */
    void set_caption(const std::string& text);

    /**
     * @brief Check whether caption output is suppressed.
     * @return True if captions should not be emitted.
     */
    bool caption_suppressed() const;

private:
    void check_annotation_ref(XmlNode element);

    // Expression context (owns the namespace for this diagram)
    ExpressionContext expr_ctx_;

    // Document objects
    pugi::xml_document svg_doc_;        // owns the SVG tree
    pugi::xml_document scratch_doc_;    // for standalone elements (annotations, etc.)
    XmlNode root_;
    XmlNode defs_;
    XmlNode diagram_element_;           // the input XML element

    // Configuration
    std::string filename_;
    std::optional<int> diagram_number_;
    OutputFormat format_;
    std::optional<std::string> output_;
    XmlNode publication_;
    bool suppress_caption_;
    Environment environment_;
    std::string caption_;
    std::string external_;

    // ID management
    bool add_id_prefix_ = false;
    std::string id_prefix_;
    std::vector<std::string> id_suffix_ = {""};
    std::unordered_map<std::string, int> ids_;

    // CTM stack
    std::vector<std::pair<CTM, BBox>> ctm_stack_;

    // Clippath stack (stores clippath IDs as strings)
    std::vector<std::string> clippaths_;

    // Scales stack
    std::vector<std::array<std::string, 2>> scale_stack_;

    // Margins and layout
    std::array<double, 4> margins_ = {0, 0, 0, 0};
    double bottomline_ = 0;
    double centerline_ = 0;

    // Defaults from publication file
    std::unordered_map<std::string, XmlNode> defaults_;

    // Reusable elements
    std::unordered_map<std::string, XmlNode> reusables_;

    // Shapes
    std::unordered_map<std::string, XmlNode> shape_dict_;

    // Annotations
    XmlNode annotations_root_;
    bool add_default_annotations_ = true;
    bool author_annotations_present_ = false;
    std::vector<XmlNode> default_annotations_;
    std::vector<XmlNode> annotation_branch_stack_;
    std::unordered_map<std::string, XmlNode> annotation_branches_;

    // Element data storage (keyed by node hash_value)
    std::unordered_map<size_t, std::unordered_map<std::string, Value>> source_data_;
    std::unordered_map<size_t, Value> saved_data_;

    // Label management
    std::unordered_map<size_t, std::tuple<XmlNode, XmlNode, CTM>> label_group_dict_;
    std::unordered_map<size_t, std::pair<double, double>> label_dims_;

    // Network coordinates
    std::unordered_map<std::string, Value> network_coords_;

    // Legends
    std::vector<void*> legends_;

    // Source element to SVG element mapping
    std::unordered_map<size_t, XmlNode> source_to_svg_map_;
};

}  // namespace prefigure
