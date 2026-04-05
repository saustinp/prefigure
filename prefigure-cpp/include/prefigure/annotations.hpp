#pragma once

#include "types.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace prefigure {

/**
 * @brief Process an <annotations> XML element by building the annotation tree.
 *
 * Annotations provide accessibility metadata (screen readers, sonification).
 * They are collected into a separate XML tree that is written alongside the SVG.
 */
void annotations(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

/**
 * @brief Recursively build annotation tree entries.
 */
void annotate(XmlNode element, Diagram& diagram, XmlNode parent = XmlNode());

/**
 * @brief Auto-generate speech annotations from diagram elements.
 */
void diagram_to_speech(XmlNode diagram_copy,
                       const std::unordered_map<size_t, XmlNode>& source_to_svg);

/**
 * @brief Extract text from a label element for speech output.
 */
std::string label_to_speech(XmlNode element);

/**
 * @brief Pronunciation overrides for hyphenated tag names.
 */
const std::unordered_map<std::string, std::string>& pronounciations();

/**
 * @brief Set of element tags that carry label text.
 */
const std::unordered_set<std::string>& labeled_elements();

/**
 * @brief Map from label sub-element tags to their spoken names.
 */
const std::unordered_map<std::string, std::string>& label_subelements();

}  // namespace prefigure
