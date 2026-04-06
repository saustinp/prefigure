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

/**
 * @brief Initialize the label rendering subsystem with appropriate backends.
 *
 * @details Creates and configures the singleton instances for math label
 * rendering (MathJax via Node.js), text measurement (Cairo), and braille
 * translation (liblouis) based on the output format and host environment.
 * Must be called once before any label processing occurs.
 *
 * @param format      The output format (SVG or Tactile), which determines
 *                    whether MathJax produces SVG fragments or braille text.
 * @param environment The host environment (CLI, PreTeXt, or Pyodide), which
 *                    selects local vs. browser-side backend implementations.
 *
 * @see AbstractMathLabels, AbstractTextMeasurements, AbstractBrailleTranslator
 */
void init(OutputFormat format, Environment environment);

/**
 * @brief Register LaTeX macro definitions for MathJax label rendering.
 *
 * @details Macros are forwarded to the active AbstractMathLabels backend
 * and become available in all subsequent math label expressions. Typically
 * called once after init() with macros read from the publication file.
 *
 * @param macros A string containing LaTeX macro definitions
 *               (e.g., "\\newcommand{\\R}{\\mathbb{R}}").
 *
 * @see AbstractMathLabels::add_macros()
 */
void add_macros(const std::string& macros);

/**
 * @brief Check whether an XML tag name is a label sub-element.
 *
 * @details Label sub-elements are inline formatting tags that appear inside
 * a label's text content: \<it\> (italic), \<b\> (bold), and \<newline\>.
 *
 * @param tag The XML tag name to test.
 * @return True if @p tag is "it", "b", or "newline".
 */
bool is_label_tag(const std::string& tag);

/**
 * @brief Evaluate ${...} expression substitutions in text.
 *
 * @details Scans @p text for occurrences of `${expr}` and replaces each
 * with the string representation of evaluating @p expr in the given
 * expression context. This allows labels to contain computed values
 * (e.g., "Area = ${pi*r^2}").
 *
 * @param text The text string potentially containing ${...} patterns.
 * @param ctx  The expression evaluation context for resolving expressions.
 * @return A new string with all ${...} patterns replaced by their values.
 *
 * @see ExpressionContext::eval()
 */
std::string evaluate_text(const std::string& text, ExpressionContext& ctx);

/**
 * @brief Process a \<label\> XML element by registering it for deferred placement.
 *
 * @details Labels use a two-phase system: during the parse pass, each label
 * is registered (text content is recorded and queued for MathJax processing),
 * but actual SVG positioning is deferred until place_labels() is called.
 * This allows batch MathJax rendering for performance and ensures that
 * label dimensions are known before positioning.
 *
 * @param element The source \<label\> XML element.
 * @param diagram The parent diagram context.
 * @param parent  The SVG parent node that will eventually contain the label.
 * @param status  The current outline rendering pass.
 *
 * @see place_labels()
 */
void label_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Position all deferred labels within the diagram.
 *
 * @details Called during diagram finalization after all elements have been
 * parsed. This function:
 * 1. Triggers batch MathJax rendering of all queued math labels.
 * 2. Measures text dimensions for non-math labels.
 * 3. Computes each label's final SVG position based on its anchor point,
 *    alignment, and offset attributes.
 * 4. Inserts the rendered label content into the SVG tree.
 *
 * For tactile output, labels are translated to braille and snapped to the
 * embossing grid.
 *
 * @param filename         Path to the source file (used for MathJax temp files).
 * @param diagram          The parent diagram context.
 * @param root             The SVG root node for inserting label content.
 * @param label_group_dict Map from element hash to (element, group, CTM) tuples
 *                         representing all deferred labels.
 *
 * @see label_element(), snap_to_embossing_grid()
 */
void place_labels(Diagram& diagram, const std::string& filename, XmlNode root,
                  std::unordered_map<size_t, std::tuple<XmlNode, XmlNode, CTM>>& label_group_dict);

/**
 * @brief Process a \<caption\> XML element.
 *
 * @details Extracts the caption text from the element and stores it in the
 * diagram for later emission. Caption output can be suppressed via the
 * Diagram's suppress_caption configuration.
 *
 * @param element The source \<caption\> XML element.
 * @param diagram The parent diagram context.
 * @param parent  The SVG parent node (unused for captions).
 * @param status  The current outline rendering pass.
 *
 * @see Diagram::set_caption(), Diagram::caption_suppressed()
 */
void caption(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Compute a compass-style alignment string from a direction vector.
 *
 * @details Maps a 2D direction vector to one of eight compass alignments
 * (N, NE, E, SE, S, SW, W, NW) plus center. The direction is quantized
 * to the nearest 45-degree sector. Used to determine where a label should
 * be placed relative to its anchor point.
 *
 * @param direction A 2D direction vector (does not need to be normalized).
 * @return An alignment string such as "ne", "s", "center", etc.
 */
std::string get_alignment_from_direction(const Point2d& direction);

/**
 * @brief Snap a coordinate to the 20 dpi embossing grid (3.6 pt spacing).
 *
 * @details For tactile/braille output, label positions must align with the
 * embossing grid to produce readable braille. This function rounds the
 * given coordinate to the nearest multiple of 3.6 points.
 *
 * @param x The coordinate value in SVG points.
 * @return The nearest grid-aligned coordinate.
 */
double snap_to_embossing_grid(double x);

// ---------------------------------------------------------------------------
// Constants (alignment lookup tables)
// ---------------------------------------------------------------------------

/**
 * @brief Get the alignment displacement lookup table for SVG labels.
 *
 * @details Returns a map from alignment string (e.g., "ne", "sw", "center")
 * to a fractional [dx, dy] offset pair. These offsets are multiplied by the
 * label dimensions to compute the final displacement from the anchor point.
 *
 * @return Const reference to the alignment-to-displacement map.
 *
 * @see get_alignment_from_direction()
 */
const std::unordered_map<std::string, std::array<double, 2>>& alignment_displacement_map();

/**
 * @brief Get the alignment displacement lookup table for braille labels.
 *
 * @details Similar to alignment_displacement_map() but with offsets tuned
 * for braille cell dimensions and embossing grid constraints.
 *
 * @return Const reference to the braille alignment-to-displacement map.
 *
 * @see snap_to_embossing_grid()
 */
const std::unordered_map<std::string, std::array<double, 2>>& braille_displacement_map();

/**
 * @brief Access the global math labels backend.
 *
 * @details Returns a pointer to the active AbstractMathLabels implementation
 * (e.g., LocalMathLabels for desktop use). The backend is created by init()
 * and persists for the lifetime of the process.
 *
 * @return Pointer to the math labels backend, or nullptr if init() has not been called.
 *
 * @see init(), AbstractMathLabels
 */
AbstractMathLabels* get_math_labels();

/**
 * @brief Access the global text measurements backend.
 *
 * @details Returns a pointer to the active AbstractTextMeasurements
 * implementation (e.g., CairoTextMeasurements for desktop use).
 *
 * @return Pointer to the text measurements backend, or nullptr if init() has not been called.
 *
 * @see init(), AbstractTextMeasurements
 */
AbstractTextMeasurements* get_text_measurements();

/**
 * @brief Access the global braille translator backend.
 *
 * @details Returns a pointer to the active AbstractBrailleTranslator
 * implementation (e.g., LocalLouisBrailleTranslator for desktop use).
 *
 * @return Pointer to the braille translator backend, or nullptr if init() has not been called.
 *
 * @see init(), AbstractBrailleTranslator
 */
AbstractBrailleTranslator* get_braille_translator();

}  // namespace prefigure
