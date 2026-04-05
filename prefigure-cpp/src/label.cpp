#include "prefigure/label.hpp"

namespace prefigure {

void label_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)element; (void)diagram; (void)parent; (void)status;
}

void caption(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)element; (void)diagram; (void)parent; (void)status;
}

bool is_label_tag(const std::string& tag) {
    (void)tag;
    return false;
}

std::string evaluate_text(const std::string& text) {
    (void)text;
    return "";
}

void place_labels(Diagram& diagram, const std::string& group_id, XmlNode element) {
    (void)diagram; (void)group_id; (void)element;
}

void init(OutputFormat format, Environment environment) {
    (void)format; (void)environment;
}

void add_macros(const std::string& macros) {
    (void)macros;
}

}  // namespace prefigure
