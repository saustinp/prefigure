#include "prefigure/annotations.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace prefigure {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static const std::unordered_map<std::string, std::string> s_pronounciations = {
    {"de-solve",            "D E solve"},
    {"define-shapes",       "define shapes"},
    {"angle-marker",        "angle marker"},
    {"area-between-curves", "area between curves"},
    {"area-under-curve",    "area under curve"},
    {"grid-axes",           "grid axes"},
    {"implicit-curve",      "implicit curve"},
    {"parametric-curve",    "parametric curve"},
    {"plot-de-solution",    "plot D E solution"},
    {"riemann-sum",         "Riemann sum"},
    {"slope-field",         "slope field"},
    {"tick-mark",           "tick mark"},
    {"tangent-line",        "tangent line"},
    {"vector-field",        "vector field"},
};

static const std::unordered_set<std::string> s_labeled_elements = {
    "label", "point", "xlabel", "ylabel",
    "angle-marker", "tick-mark", "item", "node", "edge"
};

static const std::unordered_map<std::string, std::string> s_label_subelements = {
    {"m",       "math"},
    {"b",       "bold"},
    {"it",      "italics"},
    {"plain",   "plain"},
    {"newline", "new line"},
};

const std::unordered_map<std::string, std::string>& pronounciations() {
    return s_pronounciations;
}

const std::unordered_set<std::string>& labeled_elements() {
    return s_labeled_elements;
}

const std::unordered_map<std::string, std::string>& label_subelements() {
    return s_label_subelements;
}

// ---------------------------------------------------------------------------
// Helper: convert attributes to speech
// ---------------------------------------------------------------------------

static std::string attributes_to_speech(
    const std::vector<std::pair<std::string, std::string>>& attribs)
{
    std::string result;
    for (size_t i = 0; i < attribs.size(); ++i) {
        if (i > 0) result += ", ";
        result += attribs[i].first + " has value " + attribs[i].second;
    }
    return result;
}

// ---------------------------------------------------------------------------
// annotate
// ---------------------------------------------------------------------------

void annotate(XmlNode element, Diagram& diagram, XmlNode parent) {
    std::string tag = element.name();
    if (tag.empty()) return;  // skip comments etc.

    spdlog::debug("Processing annotation with ref={}", element.attribute("ref").as_string());

    if (!parent) {
        parent = diagram.get_annotations_root();
    }

    // Handle ref attribute
    auto ref_attr = element.attribute("ref");
    if (ref_attr) {
        std::string ref = ref_attr.as_string();
        ref = diagram.prepend_id_prefix(ref);
        if (!element.attribute("id")) {
            element.append_attribute("id").set_value(ref.c_str());
        } else {
            element.attribute("id").set_value(ref.c_str());
        }
        element.remove_attribute("ref");
    } else {
        spdlog::info("An annotation has an empty attribute ref");
    }
    element.remove_attribute("annotate");

    // Check for annotation branch reference
    std::string id = element.attribute("id").as_string();
    id = diagram.prepend_id_prefix(id);
    auto branch = diagram.get_annotation_branch(id);
    if (branch) {
        annotate(branch, diagram, parent);
        return;
    }

    // Create the annotation node
    auto scratch = diagram.get_scratch();
    auto annotation = scratch.append_child("annotation");
    diagram.add_annotation(annotation);

    annotation.append_attribute("id").set_value(
        element.attribute("id").as_string("none"));

    bool active = false;

    // Copy attributes
    for (auto attr = element.first_attribute(); attr; attr = attr.next_attribute()) {
        std::string key = attr.name();
        std::string value = attr.value();
        if (key == "text") {
            active = true;
            annotation.append_attribute("speech2").set_value(value.c_str());
        } else {
            if (!annotation.attribute(key.c_str())) {
                annotation.append_attribute(key.c_str()).set_value(value.c_str());
            }
        }
    }

    // Determine element type
    XmlNode type_el;
    if (element.first_child()) {
        type_el = annotation.append_child("grouped");
    } else {
        if (active) {
            type_el = annotation.append_child("active");
        } else {
            type_el = annotation.append_child("passive");
        }
    }
    type_el.text().set(element.attribute("id").as_string());

    // Determine position
    bool toplevel = (std::string(parent.name()) == "annotations");
    auto pos = annotation.append_child("position");

    if (active) {
        if (toplevel) {
            int position = 0;
            for (auto c = parent.first_child(); c; c = c.next_sibling()) {
                position++;
            }
            pos.text().set(std::to_string(position).c_str());
        } else {
            auto children = parent.child("children");
            if (!children) {
                children = parent.append_child("children");
            }
            int position = 0;
            for (auto c = children.first_child(); c; c = c.next_sibling()) {
                position++;
            }
            position += 1;
            pos.text().set(std::to_string(position).c_str());

            auto child_el = children.append_child("active");
            child_el.text().set(annotation.attribute("id").as_string());
        }
    } else {
        pos.text().set("0");
    }

    // Register with parent
    if (!toplevel) {
        auto components = parent.child("components");
        if (!components) {
            components = parent.append_child("components");
        }
        XmlNode comp;
        if (active) {
            comp = components.append_child("active");
        } else {
            comp = components.append_child("passive");
        }
        comp.text().set(annotation.attribute("id").as_string());
    }

    // Recurse into children
    for (auto sub = element.first_child(); sub; sub = sub.next_sibling()) {
        if (sub.type() == pugi::node_element) {
            annotate(sub, diagram, annotation);
        }
    }

    // Add parent reference
    if (!toplevel) {
        auto parents = annotation.append_child("parents");
        XmlNode comp;
        if (parent.child("grouped")) {
            comp = parents.append_child("grouped");
        } else {
            comp = parents.append_child("active");
        }
        comp.text().set(parent.attribute("id").as_string());
    }

    // Handle sonification
    std::string sonify = element.attribute("sonify").as_string("no");
    if (sonify == "yes") {
        auto sonification = annotation.append_child("sonification");
        auto active_el = sonification.append_child("ACTIVE");
        active_el.text().set(element.attribute("id").as_string());
    }
}

// ---------------------------------------------------------------------------
// annotations (top-level entry point)
// ---------------------------------------------------------------------------

void annotations(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    // Tactile diagrams have no annotations (except in pyodide)
    if (diagram.output_format() == OutputFormat::Tactile &&
        diagram.get_environment() != Environment::Pyodide) {
        return;
    }

    // Initialize the annotation tree
    diagram.initialize_annotations();

    auto default_anns = diagram.get_default_annotations();
    bool defaults_added = false;

    for (auto sub = element.first_child(); sub; sub = sub.next_sibling()) {
        if (sub.type() != pugi::node_element) continue;

        // Insert default annotations before the first element
        if (!defaults_added) {
            // We can't easily insert before in pugixml iteration,
            // so we annotate defaults first
            for (auto& ann : default_anns) {
                annotate(ann, diagram);
            }
            defaults_added = true;
        }

        annotate(sub, diagram);
    }

    // If no children but there are defaults
    if (!defaults_added) {
        for (auto& ann : default_anns) {
            annotate(ann, diagram);
        }
    }
}

// ---------------------------------------------------------------------------
// label_to_speech
// ---------------------------------------------------------------------------

std::string label_to_speech(XmlNode element) {
    std::vector<std::string> strings;

    std::string text = element.text().as_string();
    // Trim
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    while (!text.empty() && text.back() == ' ') text.pop_back();
    if (!text.empty()) {
        strings.push_back(text);
    }

    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) continue;
        std::string child_tag = child.name();
        auto it = s_label_subelements.find(child_tag);
        std::string child_speech = (it != s_label_subelements.end()) ? it->second : child_tag;

        strings.push_back("begin " + child_speech);
        std::string child_text = child.text().as_string();
        while (!child_text.empty() && child_text.front() == ' ') child_text.erase(child_text.begin());
        while (!child_text.empty() && child_text.back() == ' ') child_text.pop_back();
        strings.push_back(child_text);
        strings.push_back("end " + child_speech);

        // Tail text (next sibling pcdata)
        auto next = child.next_sibling();
        if (next && next.type() == pugi::node_pcdata) {
            std::string tail = next.value();
            while (!tail.empty() && tail.front() == ' ') tail.erase(tail.begin());
            while (!tail.empty() && tail.back() == ' ') tail.pop_back();
            if (!tail.empty()) {
                strings.push_back(tail);
            }
        }
    }

    std::string result;
    for (size_t i = 0; i < strings.size(); ++i) {
        if (i > 0) result += " ";
        result += strings[i];
    }
    return result;
}

// ---------------------------------------------------------------------------
// diagram_to_speech
// ---------------------------------------------------------------------------

void diagram_to_speech(XmlNode diagram_elem,
                       const std::unordered_map<size_t, XmlNode>& source_to_svg) {
    int element_num = 0;

    // Collect all descendant elements (recursive walk, like Python's getiterator)
    std::vector<XmlNode> elements;
    std::function<void(XmlNode)> collect_descendants = [&](XmlNode node) {
        for (auto desc = node.first_child(); desc; desc = desc.next_sibling()) {
            if (desc.type() == pugi::node_element) {
                elements.push_back(desc);
                collect_descendants(desc);
            }
        }
    };
    collect_descendants(diagram_elem);

    for (auto element : elements) {
        std::string tag = element.name();

        if (tag == "annotation") continue;  // skip existing annotations

        // Check if tag is a label sub-element
        if (s_label_subelements.count(tag)) {
            element.parent().remove_child(element);
            continue;
        }

        // Save attributes before clearing
        std::vector<std::pair<std::string, std::string>> attribs;
        for (auto attr = element.first_attribute(); attr; attr = attr.next_attribute()) {
            attribs.push_back({attr.name(), attr.value()});
        }

        // Clear all attributes
        while (element.first_attribute()) {
            element.remove_attribute(element.first_attribute());
        }

        std::string intro;
        if (tag == "diagram") {
            element.append_attribute("ref").set_value("figure");
            intro = "This prefigure source file begins with a diagram having these attributes: ";
        } else if (tag == "definition") {
            element.append_attribute("ref").set_value(
                ("element-" + std::to_string(element_num)).c_str());
            std::string def_text = element.text().as_string();
            while (!def_text.empty() && def_text.front() == ' ') def_text.erase(def_text.begin());
            while (!def_text.empty() && def_text.back() == ' ') def_text.pop_back();
            intro = "A definition element defining " + def_text;
        } else if (s_labeled_elements.count(tag)) {
            element.append_attribute("ref").set_value(
                ("element-" + std::to_string(element_num)).c_str());
            auto pron_it = s_pronounciations.find(tag);
            std::string tag_speech = (pron_it != s_pronounciations.end()) ? pron_it->second : tag;
            std::string label_text = label_to_speech(element);
            if (!label_text.empty()) {
                if (attribs.empty()) {
                    intro = "A " + tag_speech + " element with label " + label_text +
                            ".  The element has no attributes.";
                } else {
                    intro = "A " + tag_speech + " element with label " + label_text +
                            ".  There are these attributes: ";
                }
            } else {
                if (attribs.empty()) {
                    intro = "A " + tag_speech + " element with no attributes.";
                } else {
                    intro = "A " + tag_speech + " element with these attributes: ";
                }
            }
            element.text().set("");
        } else {
            element.append_attribute("ref").set_value(
                ("element-" + std::to_string(element_num)).c_str());
            auto pron_it = s_pronounciations.find(tag);
            std::string tag_speech = (pron_it != s_pronounciations.end()) ? pron_it->second : tag;
            if (attribs.empty()) {
                intro = "A " + tag_speech + " element with no attributes";
            } else {
                intro = "A " + tag_speech + " element with these attributes: ";
            }
        }

        element.append_attribute("text").set_value(
            (intro + attributes_to_speech(attribs)).c_str());
        element_num++;
        element.set_name("annotation");

        // Link to SVG element if available
        auto svg_it = source_to_svg.find(element.hash_value());
        if (svg_it != source_to_svg.end()) {
            std::string svg_id = svg_it->second.attribute("id").as_string();
            if (!svg_id.empty()) {
                auto child = element.append_child("annotation");
                child.append_attribute("ref").set_value(svg_id.c_str());
            }
        }
    }
}

}  // namespace prefigure
