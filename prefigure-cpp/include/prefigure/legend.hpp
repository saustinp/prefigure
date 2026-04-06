#pragma once

#include "types.hpp"
#include "ctm.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace prefigure {

/// Forward declarations
class Diagram;

/**
 * @brief Represents a diagram legend with key/label pairs.
 *
 * Constructed during parsing from a `<legend>` XML element, then
 * positioned during finalization (after labels have been measured).
 * Each legend item consists of a color key swatch and a text label.
 *
 * The placement algorithm measures the widths of all labels (via Cairo
 * for text, MathJax for math), computes the legend box dimensions,
 * then positions it at the specified anchor with alignment displacement.
 */
class Legend {
public:
    /**
     * @brief Construct a Legend from a `<legend>` XML element.
     *
     * Parses `<li>` children to extract key swatches and label text.
     * Queues math labels for MathJax batch rendering.
     *
     * @param element The `<legend>` XML element.
     * @param diagram Parent diagram context.
     * @param parent  SVG parent node.
     * @param outline_status Outline rendering pass.
     */
    Legend(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus outline_status);

    /**
     * @brief Position the legend for SVG output.
     *
     * Measures all label dimensions, computes the legend bounding box,
     * applies alignment displacement from the anchor point, and positions
     * each key/label pair vertically with consistent spacing.
     *
     * @param diagram Parent diagram context (for coordinate transforms and text measurement).
     */
    void place_legend(Diagram& diagram);

    /**
     * @brief Position the legend for tactile (braille) output.
     *
     * Uses braille-specific spacing and larger key swatches suitable
     * for embossed output.
     *
     * @param diagram Parent diagram context.
     */
    void place_tactile_legend(Diagram& diagram);

private:
    XmlNode element_;                     ///< The original `<legend>` XML element.
    Diagram* diagram_;                    ///< Non-owning pointer to parent diagram.
    XmlNode parent_;                      ///< SVG parent node for output.
    XmlNode group_;                       ///< The `<g>` node containing all legend SVG output.
    bool outline_;                        ///< True if outline rendering was requested.
    bool tactile_;                        ///< True if rendering in tactile mode.

    Point2d def_anchor_;                  ///< Default anchor point in user coordinates.
    std::array<double, 2> displacement_;  ///< Alignment displacement (fractions of legend width/height).
    double key_width_ = 0;               ///< Width of key swatches in SVG units.
    double line_width_ = 24;             ///< Line segment width in key swatches.

    /// A legend item: the color key swatch and its associated label node.
    struct ItemEntry {
        XmlNode key;    ///< SVG node for the color key swatch.
        XmlNode label;  ///< SVG node for the label text/math.
    };
    std::vector<std::pair<XmlNode, ItemEntry>> li_items_; ///< Ordered list of (li_element, item_entry) pairs.
};

/**
 * @brief Process a `<legend>` XML element.
 *
 * Creates a Legend object and registers it with the diagram for
 * deferred positioning during place_labels().
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node.
 * @param status  Outline rendering pass.
 */
void legend_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
