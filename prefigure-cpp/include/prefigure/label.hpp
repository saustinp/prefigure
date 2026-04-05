#pragma once

#include "types.hpp"
#include "label_tools.hpp"
#include "ctm.hpp"

#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace prefigure {

// Forward declarations
class Legend;
class ExpressionContext;

// ---------------------------------------------------------------------------
// Module-level state (singletons for the label subsystem)
// ---------------------------------------------------------------------------

/// Initialize the label rendering subsystem with appropriate backends.
void init(OutputFormat format, Environment environment);

/// Register LaTeX macro definitions for label rendering.
void add_macros(const std::string& macros);

/// Check whether an XML tag is a label sub-element (it, b, newline).
bool is_label_tag(const std::string& tag);

/// Evaluate ${...} expression substitutions in text using the given expression context.
std::string evaluate_text(const std::string& text, ExpressionContext& ctx);

/// Process a <label> element: register it for deferred placement.
void label_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/// Position all deferred labels within the diagram (called during finalization).
void place_labels(Diagram& diagram, const std::string& filename, XmlNode root,
                  std::unordered_map<size_t, std::tuple<XmlNode, XmlNode, CTM>>& label_group_dict);

/// Process a <caption> element.
void caption(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/// Compute alignment from a direction vector (8-point compass).
std::string get_alignment_from_direction(const Point2d& direction);

/// Snap a coordinate to the 20dpi embossing grid (3.6pt spacing).
double snap_to_embossing_grid(double x);

// ---------------------------------------------------------------------------
// Constants (alignment lookup tables)
// ---------------------------------------------------------------------------

/// Alignment displacement for SVG labels (fractional offsets for 3x3 grid).
const std::unordered_map<std::string, std::array<double, 2>>& alignment_displacement_map();

/// Alignment displacement for braille labels.
const std::unordered_map<std::string, std::array<double, 2>>& braille_displacement_map();

/// Access the global math labels backend.
AbstractMathLabels* get_math_labels();

/// Access the global text measurements backend.
AbstractTextMeasurements* get_text_measurements();

/// Access the global braille translator backend.
AbstractBrailleTranslator* get_braille_translator();

}  // namespace prefigure
