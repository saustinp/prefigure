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
 * Constructed during parsing, then positioned during finalization
 * (after labels have been measured).
 */
class Legend {
public:
    /**
     * @brief Construct a Legend from a <legend> XML element.
     */
    Legend(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus outline_status);

    /// Position the legend for SVG output.
    void place_legend(Diagram& diagram);

    /// Position the legend for tactile output.
    void place_tactile_legend(Diagram& diagram);

private:
    XmlNode element_;
    Diagram* diagram_;
    XmlNode parent_;
    XmlNode group_;
    bool outline_;
    bool tactile_;

    Point2d def_anchor_;
    std::array<double, 2> displacement_;
    double key_width_ = 0;
    double line_width_ = 24;

    /// Each item maps to [key_node, label_node]
    struct ItemEntry {
        XmlNode key;
        XmlNode label;
    };
    std::vector<std::pair<XmlNode, ItemEntry>> li_items_;
};

/**
 * @brief Process a <legend> XML element.
 */
void legend_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
