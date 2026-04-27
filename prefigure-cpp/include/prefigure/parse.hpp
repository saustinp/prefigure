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
 * @param return_string   If true, serialize to strings instead of writing files.
 *
 * @return When return_string is true, a pair of (SVG string, optional annotations XML
 *         string).  When return_string is false (file mode), `{"", std::nullopt}`.
 *         On error at any stage, `{"", std::nullopt}`.
 *
 * @see parse() for the higher-level entry point that finds `<diagram>` elements in a file.
 * @see Diagram for the object that manages the rendering pipeline.
 */
std::pair<std::string, std::optional<std::string>>
mk_diagram(XmlNode element,
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

/**
 * @brief Build a diagram from an XML string and return the SVG and optional annotations.
 *
 * This is the C++ equivalent of Python's `engine.build_from_string()`.
 * It parses the XML string, finds the first `<diagram>` element, strips
 * namespace prefixes, checks for duplicate handles, and renders the
 * diagram in memory.
 *
 * @param format_str  Output format: "svg" or "tactile".
 * @param xml_string  The complete XML source containing a `<diagram>` element.
 * @param environment The host environment name (default: "pyodide").
 *
 * @return A pair of (SVG string, optional annotations XML string).  On parse
 *         failure or missing `<diagram>`, `{"", std::nullopt}`.  When the
 *         diagram has no annotations, the second element is `std::nullopt`.
 *
 * @see mk_diagram() for the rendering pipeline.
 * @see parse() for the file-based entry point.
 */
std::pair<std::string, std::optional<std::string>>
build_from_string(const std::string& format_str,
                  const std::string& xml_string,
                  const std::string& environment = "pyodide");

}  // namespace prefigure
