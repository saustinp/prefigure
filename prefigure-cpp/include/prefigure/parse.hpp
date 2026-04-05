#pragma once

#include "types.hpp"

#include <optional>
#include <set>
#include <string>

namespace prefigure {

/**
 * @brief Build and render a single diagram from an XML `<diagram>` element.
 *
 * This is the core pipeline for one diagram: construct a Diagram, call
 * begin_figure(), parse(), place_labels(), annotate_source(), and
 * end_figure() (or end_figure_to_string()).  Errors at each stage are
 * caught and logged rather than propagated.
 *
 * @param element         The `<diagram>` XML node.
 * @param format          SVG or Tactile output mode.
 * @param publication     The `<prefigure>` node from the publication file, or null.
 * @param filename        Path to the source XML file (used for output naming).
 * @param suppress_caption If true, suppress caption output.
 * @param diagram_number  Index when multiple diagrams exist in one file; nullopt for single.
 * @param environment     The host environment (PreTeXt, CLI, or Pyodide).
 * @param return_string   If true, serialize to string instead of writing files
 *                        (the result is currently discarded -- reserved for future use).
 *
 * @see parse() for the higher-level entry point that finds `<diagram>` elements in a file.
 * @see Diagram for the object that manages the rendering pipeline.
 */
void mk_diagram(XmlNode element,
                OutputFormat format,
                XmlNode publication,
                const std::string& filename,
                bool suppress_caption,
                std::optional<int> diagram_number,
                Environment environment,
                bool return_string = false);

/**
 * @brief Parse a PreFigure XML file, locate all `<diagram>` elements, and render each one.
 *
 * This is the primary entry point for batch processing.  It loads the XML file
 * and an optional publication file, strips XML namespace prefixes, checks for
 * duplicate handles, and calls mk_diagram() for each `<diagram>` found.
 *
 * @param filename        Path to the PreFigure XML source file.
 * @param format          SVG or Tactile output mode (default: SVG).
 * @param pub_file        Path to the publication defaults file (empty to skip).
 * @param suppress_caption If true, suppress caption output in all diagrams.
 * @param environment     The host environment (default: Pretext).
 *
 * @note When a file contains multiple `<diagram>` elements, each receives a
 *       sequential diagram_number used in the output filename suffix.
 */
void parse(const std::string& filename,
           OutputFormat format = OutputFormat::SVG,
           const std::string& pub_file = "",
           bool suppress_caption = false,
           Environment environment = Environment::Pretext);

/**
 * @brief Recursively check for duplicate `@id` and `@at` attribute values in the element tree.
 *
 * Logs a warning for each duplicate handle found.  This is called once per
 * single-diagram file to catch authoring errors early.
 *
 * @param element The root element to search from.
 * @param handles A mutable set that accumulates seen handle strings across recursive calls.
 */
void check_duplicate_handles(XmlNode element, std::set<std::string>& handles);

}  // namespace prefigure
