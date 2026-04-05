#include "prefigure/clip.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <format>
#include <string>

namespace prefigure {

void clip(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus outline_status) {
    auto shape_attr = element.attribute("shape");
    if (!shape_attr) {
        spdlog::error("A <clip> tag needs a @shape attribute");
        return;
    }

    std::string shape_ref = diagram.prepend_id_prefix(shape_attr.value());
    auto shape = diagram.recall_shape(shape_ref);
    if (!shape) {
        spdlog::error("Cannot clip to shape whose name is {}", shape_ref);
        return;
    }

    // Create a clipPath element containing the shape
    pugi::xml_document clip_doc;
    auto clip_elem = clip_doc.append_child("clipPath");
    clip_elem.append_copy(shape);
    std::string clip_id = shape_ref + "-clip";
    clip_elem.append_attribute("id").set_value(clip_id.c_str());

    diagram.add_reusable(clip_elem);

    // Create a group clipped to the shape
    auto group = parent.append_child("g");
    std::string clip_url = std::format("url(#{})", clip_elem.attribute("id").value());
    group.append_attribute("clip-path").set_value(clip_url.c_str());

    diagram.parse(element, group, outline_status);
}

}  // namespace prefigure
