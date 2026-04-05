#pragma once

#include "types.hpp"

namespace prefigure {

/**
 * @brief Render an `<annotations>` XML element by building the annotation tree.
 *
 * Processes annotation metadata used for accessibility (screen readers,
 * sonification).  Annotations are collected into a separate XML tree
 * that is written alongside the SVG output.
 *
 * @par XML Attributes
 * - Child `<annotation>` elements with `@ref`, `@text`, `@speech`, `@sonify`, etc.
 *
 * @par SVG Output
 * None directly -- annotations are written to a separate XML file.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param parent  SVG parent node (unused for annotation-only output).
 * @param status  Outline rendering pass.
 *
 * @see Diagram::initialize_annotations(), Diagram::add_annotation()
 *
 * @note Currently a stub implementation.
 */
void annotations(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
