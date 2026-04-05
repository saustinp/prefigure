#include "prefigure/path_element.hpp"

namespace prefigure {

void path_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)element; (void)diagram; (void)parent; (void)status;
}

bool is_path_tag(const std::string& tag) {
    (void)tag;
    return false;
}

}  // namespace prefigure
