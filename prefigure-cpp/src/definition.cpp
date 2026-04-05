#include "prefigure/definition.hpp"
#include "prefigure/user_namespace.hpp"

#include <spdlog/spdlog.h>

#include <string>

namespace prefigure {

void definition(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)parent;
    (void)status;

    // Get the definition text from the element's text content
    auto text_val = element.child_value();
    if (!text_val || std::string(text_val).empty()) {
        spdlog::error("PreFigure is ignoring an empty definition");
        return;
    }

    std::string text(text_val);
    // Trim whitespace
    auto start = text.find_first_not_of(" \t\n\r");
    auto end = text.find_last_not_of(" \t\n\r");
    if (start == std::string::npos) {
        spdlog::error("PreFigure is ignoring an empty definition");
        return;
    }
    text = text.substr(start, end - start + 1);

    bool substitution = true;
    auto sub_attr = element.attribute("substitution");
    if (sub_attr && std::string(sub_attr.value()) == "no") {
        substitution = false;
    }

    // TODO: Get ExpressionContext from diagram and call define()
    // For now this is a stub that will be connected when Diagram is implemented
    // ctx->define(text, substitution);
    (void)diagram;
    (void)substitution;
    (void)text;
}

void definition_derivative(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)parent;
    (void)status;

    auto func_attr = element.attribute("function");
    auto name_attr = element.attribute("name");

    if (!func_attr) {
        spdlog::error("A <derivative> element requires a @function attribute");
        return;
    }
    if (!name_attr) {
        spdlog::error("A <derivative> element requires a @name attribute");
        return;
    }

    // TODO: Get ExpressionContext from diagram and call register_derivative()
    // ctx->register_derivative(func_attr.value(), name_attr.value());
    (void)diagram;
}

}  // namespace prefigure
