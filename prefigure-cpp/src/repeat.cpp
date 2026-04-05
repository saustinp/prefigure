#include "prefigure/repeat.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/group.hpp"
#include "prefigure/label.hpp"
#include "prefigure/utilities.hpp"
#include "prefigure/user_namespace.hpp"

#include <spdlog/spdlog.h>

#include <sstream>
#include <string>
#include <vector>

namespace prefigure {

// Substitution map for common disallowed characters
static const std::unordered_map<char, char> epub_dict = {
    {'(', 'p'}, {')', 'q'}, {'[', 'p'}, {']', 'q'},
    {'{', 'p'}, {'}', 'q'}, {',', 'c'}, {'.', 'd'},
    {'=', '_'}, {'#', 'h'}
};

static bool is_epub_safe(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || ch == '-';
}

std::string epub_clean(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char ch : s) {
        if (is_epub_safe(ch)) {
            result += ch;
        } else {
            auto it = epub_dict.find(ch);
            if (it != epub_dict.end()) {
                result += it->second;
            } else {
                result += '_';
            }
        }
    }
    return result;
}

void repeat_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus outline_status) {
    auto param_attr = element.attribute("parameter");
    if (!param_attr) {
        spdlog::error("A <repeat> element needs a @parameter attribute");
        return;
    }

    std::string parameter = param_attr.value();
    std::string var;
    bool count_mode = false;
    int start_val = 0, stop_val = 0;
    std::vector<std::string> iterator_strs;
    Eigen::VectorXd iterator_vec;
    bool use_vec = false;

    try {
        // Try "var=start..stop" syntax first
        auto eq_pos = parameter.find('=');
        if (eq_pos != std::string::npos) {
            var = parameter.substr(0, eq_pos);
            // Trim var
            auto s = var.find_first_not_of(" \t");
            auto e = var.find_last_not_of(" \t");
            if (s != std::string::npos) var = var.substr(s, e - s + 1);

            std::string expr = parameter.substr(eq_pos + 1);
            auto dot_pos = expr.find("..");
            if (dot_pos != std::string::npos) {
                std::string start_str = expr.substr(0, dot_pos);
                std::string stop_str = expr.substr(dot_pos + 2);
                start_val = static_cast<int>(diagram.expr_ctx().eval(start_str).to_double());
                stop_val = static_cast<int>(diagram.expr_ctx().eval(stop_str).to_double());
                count_mode = true;
            } else {
                spdlog::error("Unable to parse parameter {} in <repeat>", parameter);
                return;
            }
        } else {
            // "var in collection" syntax
            // Split by whitespace
            std::istringstream iss(parameter);
            std::vector<std::string> fields;
            std::string token;
            while (iss >> token) {
                fields.push_back(token);
            }
            if (fields.size() < 3 || fields[1] != "in") {
                spdlog::error("Unable to parse parameter {} in <repeat>", parameter);
                return;
            }
            var = fields[0];
            // Rejoin everything after "in"
            std::string collection_str;
            for (size_t i = 2; i < fields.size(); ++i) {
                if (i > 2) collection_str += " ";
                collection_str += fields[i];
            }
            Value coll_val = diagram.expr_ctx().eval(collection_str);
            if (coll_val.is_vector()) {
                iterator_vec = coll_val.as_vector();
                use_vec = true;
            } else {
                spdlog::error("Unable to parse parameter {} in <repeat>", parameter);
                return;
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Unable to parse parameter {} in <repeat>: {}", parameter, e.what());
        return;
    }

    // Deep copy the element's children before we modify it.
    // We use a scratch document to hold the copies.
    pugi::xml_document element_cp_doc;
    auto element_cp = element_cp_doc.append_child("repeat-copy");
    // Copy all attributes
    for (auto attr = element.first_attribute(); attr; attr = attr.next_attribute()) {
        element_cp.append_attribute(attr.name()).set_value(attr.value());
    }
    // Copy all children
    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        element_cp.append_copy(child);
    }

    // Transform this element into a group
    auto outline_attr = element.attribute("outline");
    std::string outline_val = outline_attr ? outline_attr.value() : "";
    auto id_attr = element.attribute("id");
    std::string id_val = id_attr ? id_attr.value() : "";

    // Clear the element and convert to group
    // Remove all children
    while (element.first_child()) {
        element.remove_child(element.first_child());
    }
    // Remove all attributes except the ones we want to keep
    while (element.first_attribute()) {
        element.remove_attribute(element.first_attribute());
    }
    element.set_name("group");

    if (!outline_val.empty()) {
        element.append_attribute("outline").set_value(outline_val.c_str());
    }
    if (!id_val.empty()) {
        std::string prefixed_id = diagram.prepend_id_prefix(id_val);
        element.append_attribute("id").set_value(prefixed_id.c_str());
        // Also update element_cp
        if (element_cp.attribute("id")) {
            element_cp.attribute("id").set_value(prefixed_id.c_str());
        } else {
            element_cp.append_attribute("id").set_value(prefixed_id.c_str());
        }
    }

    // Determine iteration count
    int num_iterations;
    if (count_mode) {
        num_iterations = stop_val - start_val + 1;
    } else {
        num_iterations = static_cast<int>(iterator_vec.size());
    }

    for (int num = 0; num < num_iterations; ++num) {
        std::string k_str;
        if (count_mode) {
            int k = start_val + num;
            k_str = std::to_string(k);
        } else if (use_vec) {
            double k = iterator_vec[num];
            // Format the value
            if (k == std::floor(k)) {
                k_str = std::to_string(static_cast<int>(k));
            } else {
                k_str = std::to_string(k);
            }
        }

        std::string k_str_clean = epub_clean(k_str);

        std::string suffix_str;
        if (count_mode) {
            suffix_str = var + "_" + k_str_clean;
        } else {
            suffix_str = var + "_" + std::to_string(num);
        }

        // Create a <definition> child with the variable assignment
        auto def = element.append_child("definition");
        std::string def_text = var + "=" + k_str;
        def.append_child(pugi::node_pcdata).set_value(def_text.c_str());
        def.append_attribute("id-suffix").set_value(suffix_str.c_str());

        // Copy the original children under this definition
        for (auto child = element_cp.first_child(); child; child = child.next_sibling()) {
            def.append_copy(child);
        }
    }

    // Handle annotation
    XmlNode annotation;
    bool has_annotation = false;
    std::string annotate_val = element_cp.attribute("annotate")
        ? element_cp.attribute("annotate").value() : "no";
    if (annotate_val == "yes" && outline_status != OutlineStatus::AddOutline) {
        pugi::xml_document ann_doc;
        annotation = ann_doc.append_child("annotation");
        const char* attribs[] = {"id", "text", "circular", "sonify", "speech"};
        for (const char* a : attribs) {
            auto attr = element_cp.attribute(a);
            if (attr) {
                annotation.append_attribute(a).set_value(attr.value());
            }
        }
        auto text_a = annotation.attribute("text");
        if (text_a) {
            text_a.set_value(evaluate_text(text_a.value()).c_str());
        }
        auto speech_a = annotation.attribute("speech");
        if (speech_a) {
            speech_a.set_value(evaluate_text(speech_a.value()).c_str());
        }
        diagram.push_to_annotation_branch(annotation);
        has_annotation = true;
    }

    // Process as a group
    group(element, diagram, parent, outline_status);

    if (has_annotation) {
        diagram.pop_from_annotation_branch();
    }
}

}  // namespace prefigure
