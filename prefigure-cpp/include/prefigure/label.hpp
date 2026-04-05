#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

/**
 * @brief Render a `<label>` XML element as SVG.
 *
 * Adds a text label (typically rendered via MathJax or a tactile renderer)
 * at a specified position, with configurable alignment, offset, and rotation.
 *
 * @par XML Attributes
 * - `p` or `at` (required): Position expression for the label anchor.
 * - `alignment` (optional, default: "southeast"): Label placement relative to anchor.
 * - `offset` (optional): Pixel offset from the anchor point.
 * - `rotate` (optional): Rotation angle for the label text.
 * - Text content: The label text (may include LaTeX math).
 *
 * @par SVG Output
 * Creates a `<g>` element containing the rendered label content.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @note Currently a stub implementation.
 */
void label_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Render a `<caption>` XML element by setting the diagram's caption text.
 *
 * @par XML Attributes
 * - Text content: The caption string.
 *
 * @par SVG Output
 * None directly -- sets caption metadata on the Diagram.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node (unused).
 * @param status  Outline rendering pass (unused).
 *
 * @note Currently a stub implementation.
 */
void caption(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Check whether an XML tag name corresponds to a label-related element.
 *
 * @param tag The tag name to test.
 * @return True if the tag is a label element tag.
 *
 * @note Currently a stub that always returns false.
 */
bool is_label_tag(const std::string& tag);

/**
 * @brief Evaluate ${...} expression substitutions in a text string.
 *
 * Scans the input for `${expr}` patterns and replaces each with the
 * evaluated result from the expression context.
 *
 * @param text The input text, possibly containing `${...}` expressions.
 * @return The text with all expressions evaluated and substituted.
 *
 * @note Currently a stub that returns an empty string.
 */
std::string evaluate_text(const std::string& text);

/**
 * @brief Position all deferred labels within a diagram.
 *
 * Called during the finalization phase to compute final positions for
 * labels that were registered during the parse phase.
 *
 * @param diagram  The diagram context.
 * @param group_id The SVG group ID containing the labels.
 * @param element  The root element for label placement.
 *
 * @note Currently a stub implementation.
 */
void place_labels(Diagram& diagram, const std::string& group_id, XmlNode element);

/**
 * @brief Initialize the label rendering subsystem.
 *
 * Sets up the label renderer appropriate for the given output format
 * and environment (e.g., MathJax for SVG, tactile renderer for embossed).
 *
 * @param format      The output format (SVG or Tactile).
 * @param environment The host environment.
 *
 * @note Currently a stub implementation.
 */
void init(OutputFormat format, Environment environment);

/**
 * @brief Register LaTeX macro definitions for label rendering.
 *
 * @param macros A string of LaTeX macro definitions (from the publication file).
 *
 * @note Currently a stub implementation.
 */
void add_macros(const std::string& macros);

}  // namespace prefigure
