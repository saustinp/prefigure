#include "prefigure/parse.hpp"
#include "prefigure/diagram.hpp"

#include <spdlog/spdlog.h>
#include <pugixml.hpp>

#include <set>
#include <string>

namespace prefigure {

void mk_diagram(XmlNode element,
                OutputFormat format,
                XmlNode publication,
                const std::string& filename,
                bool suppress_caption,
                std::optional<int> diagram_number,
                Environment environment,
                bool return_string) {

    std::optional<std::string> output = std::nullopt;
    Diagram diag(element, filename, diagram_number,
                 format, output, publication, suppress_caption,
                 environment);

    spdlog::debug("Initializing PreFigure diagram");
    try {
        diag.begin_figure();
    } catch (const std::exception& e) {
        spdlog::error("There was a problem initializing the PreFigure diagram");
        spdlog::error("Error: {}", e.what());
        return;
    }

    spdlog::debug("Processing PreFigure elements");
    try {
        diag.parse();
    } catch (const std::exception& e) {
        spdlog::error("There was a problem parsing a PreFigure element");
        spdlog::error("Error: {}", e.what());
        return;
    }

    spdlog::debug("Positioning labels");
    try {
        diag.place_labels();
    } catch (const std::exception& e) {
        spdlog::error("There was a problem placing the labels in the diagram");
        spdlog::error("Error: {}", e.what());
        return;
    }

    spdlog::debug("Writing the diagram and any annotations");
    diag.annotate_source();
    try {
        if (return_string) {
            // The string result would need to be returned; for now just generate
            auto [svg, ann] = diag.end_figure_to_string();
            (void)svg; (void)ann;
        } else {
            diag.end_figure();
        }
    } catch (const std::exception& e) {
        spdlog::error("There was a problem finishing the diagram");
        spdlog::error("Error: {}", e.what());
        return;
    }
}

void parse(const std::string& filename,
           OutputFormat format,
           const std::string& pub_file,
           bool suppress_caption,
           Environment environment) {

    // Load the publication file if provided
    XmlNode publication;
    pugi::xml_document pub_doc;
    if (!pub_file.empty()) {
        auto result = pub_doc.load_file(pub_file.c_str());
        if (!result) {
            spdlog::error("Unable to parse publication file {}", pub_file);
            return;
        }
        // Look for <prefigure> element (with or without namespace)
        XmlNode pub_root;
        // Try without namespace first
        pub_root = pub_doc.child("prefigure");
        if (!pub_root) {
            // Search recursively for any element named "prefigure" (possibly with ns prefix)
            pub_root = pub_doc.find_node([](pugi::xml_node n) {
                std::string name = n.name();
                // Handle namespace prefix
                auto pos = name.find(':');
                std::string local = (pos != std::string::npos) ? name.substr(pos + 1) : name;
                return local == "prefigure";
            });
        }
        if (pub_root) {
            // Strip namespaces from children
            for (auto child = pub_root.first_child(); child; child = child.next_sibling()) {
                if (child.type() == pugi::node_comment) continue;
                std::string tag = child.name();
                auto colon = tag.find(':');
                if (colon != std::string::npos) {
                    child.set_name(tag.substr(colon + 1).c_str());
                }
            }
            publication = pub_root;
        } else {
            spdlog::warn("Publication file should have a <prefigure> element");
        }
    }

    // Parse the XML file
    pugi::xml_document doc;
    auto result = doc.load_file(filename.c_str());
    if (!result) {
        spdlog::error("Unable to parse file {}", filename);
        return;
    }

    // Helper lambda to strip namespace prefix from a node name
    auto strip_ns = [](pugi::xml_node node) {
        std::string name = node.name();
        auto colon = name.find(':');
        if (colon != std::string::npos) {
            node.set_name(name.substr(colon + 1).c_str());
        }
    };

    // Helper to get local name without modifying
    auto local_name = [](pugi::xml_node node) -> std::string {
        std::string name = node.name();
        auto colon = name.find(':');
        return (colon != std::string::npos) ? name.substr(colon + 1) : name;
    };

    // Recursive helper to strip namespaces from all element descendants
    std::function<void(pugi::xml_node)> strip_ns_recursive = [&](pugi::xml_node node) {
        for (auto child = node.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) continue;
            strip_ns(child);
            strip_ns_recursive(child);
        }
    };

    // Find <diagram> elements (with or without namespace)
    // Search through the document tree for elements named "diagram"
    std::vector<XmlNode> diagrams;
    std::function<void(pugi::xml_node)> find_diagrams = [&](pugi::xml_node node) {
        for (auto child = node.first_child(); child; child = child.next_sibling()) {
            if (child.type() != pugi::node_element) continue;
            if (local_name(child) == "diagram") {
                diagrams.push_back(child);
            } else {
                find_diagrams(child);
            }
        }
    };
    find_diagrams(doc);

    for (size_t idx = 0; idx < diagrams.size(); ++idx) {
        auto element = diagrams[idx];

        // Strip namespace prefixes from all descendant elements
        strip_ns(element);
        strip_ns_recursive(element);

        std::optional<int> diagram_number;
        if (diagrams.size() == 1) {
            diagram_number = std::nullopt;
            std::set<std::string> handles;
            check_duplicate_handles(element, handles);
        } else {
            diagram_number = static_cast<int>(idx);
        }

        mk_diagram(element, format, publication,
                    filename, suppress_caption, diagram_number,
                    environment);
    }
}

void check_duplicate_handles(XmlNode element, std::set<std::string>& handles) {
    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() != pugi::node_element) continue;

        auto id1 = child.attribute("id");
        auto id2 = child.attribute("at");

        for (auto attr : {id1, id2}) {
            if (attr) {
                std::string id = attr.value();
                if (!id.empty()) {
                    if (handles.count(id)) {
                        spdlog::warn("Duplicate handle: {}.  Unexpected behavior could result.", id);
                    } else {
                        handles.insert(id);
                    }
                }
            }
        }

        check_duplicate_handles(child, handles);
    }
}

}  // namespace prefigure
