#pragma once

#include "types.hpp"

namespace prefigure {

// Handler for <definition> XML elements
// Registers mathematical definitions in the expression context
void definition(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

// Handler for <derivative> XML elements
// Computes symbolic derivative of a function
void definition_derivative(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status);

}  // namespace prefigure
