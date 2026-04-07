#include "prefigure/diagram.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/tags.hpp"
#include "prefigure/utilities.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/label.hpp"
#include "prefigure/legend.hpp"
#include "prefigure/repeat.hpp"
#include "prefigure/user_namespace.hpp"

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <format>
#include <functional>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace prefigure {

// Regular expression to check if id is EPUB compliant
static const std::regex epub_id_check("^[A-Za-z0-9_-]+$");

Diagram::Diagram(XmlNode diagram_element, const std::string& filename,
                 std::optional<int> diagram_number, OutputFormat format,
                 std::optional<std::string> output, XmlNode publication,
                 bool suppress_caption, Environment environment)
    : diagram_element_(diagram_element)
    , filename_(filename)
    , diagram_number_(diagram_number)
    , format_(format)
    , output_(output)
    , publication_(publication)
    , suppress_caption_(suppress_caption)
    , environment_(environment)
{
    // Set up add_default_annotations based on environment
    add_default_annotations_ = true;
    if (environment_ == Environment::Pyodide) {
        // Check if there are any <annotations> children
        bool has_annotations = false;
        for (auto desc = diagram_element_.first_child(); desc; desc = desc.next_sibling()) {
            if (std::string(desc.name()) == "annotations") {
                has_annotations = true;
                break;
            }
        }
        // Also search recursively
        if (!has_annotations) {
            auto ann = diagram_element_.find_node([](pugi::xml_node n) {
                return std::string(n.name()) == "annotations";
            });
            has_annotations = !ann.empty();
        }
        if (!has_annotations) {
            add_default_annotations_ = false;
        }
    }

    // Set the diagram pointer for math_utilities
    math_set_diagram(this);

    // Initialize label module
    init(format_, environment_);

    // Create the SVG root element
    root_ = svg_doc_.append_child("svg");
    root_.append_attribute("xmlns").set_value("http://www.w3.org/2000/svg");

    // Set up ID prefix
    add_id_prefix_ = false;
    std::string figure_id = "figure";
    if (format_ != OutputFormat::Tactile) {
        add_id_prefix_ = true;
        if (environment_ == Environment::Pyodide) {
            // Hash-based prefix using current time
            auto now = std::chrono::high_resolution_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count();
            // Simple hash: use lower bits of nanosecond count
            std::string hash_str = std::to_string(ns);
            // Take last 8 chars for a compact unique prefix
            if (hash_str.size() > 8) {
                hash_str = hash_str.substr(hash_str.size() - 8);
            }
            id_prefix_ = "prefig-" + hash_str + "-";
        } else {
            std::filesystem::path p(filename_);
            id_prefix_ = p.stem().string() + "-";
            id_prefix_ = epub_clean(id_prefix_);
        }
        figure_id = id_prefix_ + figure_id;
    }

    id_suffix_ = {""};

    // Add the ID to root
    std::string diag_id = get_attr(diagram_element_, "id", figure_id);
    add_id(root_, diag_id);

    // Prepare annotations
    annotations_root_ = XmlNode();  // null node
    default_annotations_.clear();
    annotation_branches_.clear();
    annotation_branch_stack_.clear();

    // Initialize data structures
    shape_dict_.clear();
    saved_data_.clear();
    ids_.clear();
    reusables_.clear();
    network_coords_.clear();
    clippaths_.clear();
    scale_stack_.clear();
    label_dims_.clear();

    // Read in defaults from publication file
    external_.clear();
    defaults_.clear();
    if (publication_) {
        for (auto child = publication_.first_child(); child; child = child.next_sibling()) {
            std::string tag = child.name();
            if (tag == "external-root") {
                spdlog::warn("<external-root> in publication file is deprecated");
                spdlog::warn("Use <directories> instead");
                spdlog::warn("See \"Working with data\" in the PreFigure documentation");
                auto name_attr = child.attribute("name");
                if (name_attr) {
                    external_ = name_attr.value();
                } else {
                    spdlog::warn("<external-root> in publication file needs a @name");
                }
                continue;
            }
            defaults_[tag] = child;
        }
        // Check for <directories> element
        auto dirs = publication_.find_node([](pugi::xml_node n) {
            return std::string(n.name()) == "directories";
        });
        if (dirs) {
            auto data_attr = dirs.attribute("data");
            if (data_attr) {
                external_ = data_attr.value();
            }
        }
    }

    // Process <templates> in diagram element
    // Collect all <templates> elements, then remove them from the tree
    std::vector<XmlNode> templates_list;
    for (auto child = diagram_element_.first_child(); child; child = child.next_sibling()) {
        if (std::string(child.name()) == "templates") {
            templates_list.push_back(child);
        }
    }
    // Also search deeper (xpath-like)
    if (templates_list.empty()) {
        auto tmpl = diagram_element_.find_node([](pugi::xml_node n) {
            return std::string(n.name()) == "templates";
        });
        if (tmpl) {
            templates_list.push_back(tmpl);
        }
    }

    XmlNode templates_element;
    if (!templates_list.empty()) {
        templates_element = templates_list[0];
        // Remove all templates elements from parent
        for (auto& tmpl : templates_list) {
            tmpl.parent().remove_child(tmpl);
        }
        // Add children of the first templates element to defaults
        for (auto child = templates_element.first_child(); child; child = child.next_sibling()) {
            defaults_[child.name()] = child;
        }
    }

    // Check for author annotations
    author_annotations_present_ = false;
    auto ann_node = diagram_element_.find_node([](pugi::xml_node n) {
        return std::string(n.name()) == "annotations";
    });
    if (ann_node) {
        author_annotations_present_ = true;
        check_annotation_ref(ann_node);
    }

    // Handle macros from defaults
    auto macros_it = defaults_.find("macros");
    if (macros_it != defaults_.end()) {
        auto text = macros_it->second.child_value();
        if (text && std::string(text).size() > 0) {
            add_macros(text);
        }
    }
}

void Diagram::check_annotation_ref(XmlNode element) {
    auto ref_attr = element.attribute("ref");
    if (ref_attr) {
        std::string ref = ref_attr.value();
        if (!std::regex_match(ref, epub_id_check)) {
            spdlog::error("@ref {} in an annotation has characters disallowed by EPUB", ref);
            spdlog::error("  We will replace these characters but there may be unexpected behavior");
            spdlog::error("  Search for EPUB in the PreFigure documentation https://prefigure.org");
            ref = epub_clean(ref);
            ref_attr.set_value(ref.c_str());
        }
    }
    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        check_annotation_ref(child);
    }
}

void Diagram::begin_figure() {
    // Parse dimensions
    double width = 0, height = 0;
    auto dims_attr = diagram_element_.attribute("dimensions");
    try {
        if (!dims_attr) {
            auto w_attr = diagram_element_.attribute("width");
            auto h_attr = diagram_element_.attribute("height");
            if (!w_attr || !h_attr) {
                spdlog::error("Unable to parse the dimensions of this diagram");
                return;
            }
            width = expr_ctx_.eval(w_attr.value()).to_double();
            height = expr_ctx_.eval(h_attr.value()).to_double();
        } else {
            Value dims_val = expr_ctx_.eval(dims_attr.value());
            if (dims_val.is_vector()) {
                auto& v = dims_val.as_vector();
                width = v[0];
                height = v[1];
            } else {
                spdlog::error("Unable to parse the dimensions of this diagram");
                return;
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("Unable to parse the dimensions of this diagram: {}", e.what());
        return;
    }

    // Parse margins
    std::string margins_str = get_attr(diagram_element_, "margins", "[0,0,0,0]");
    if (format_ == OutputFormat::Tactile) {
        margins_str = get_attr(diagram_element_, "tactile-margins", margins_str);
    }
    try {
        Value margins_val = expr_ctx_.eval(margins_str);
        if (margins_val.is_vector()) {
            auto& v = margins_val.as_vector();
            margins_ = {v[0], v[1], v[2], v[3]};
        } else if (margins_val.is_double()) {
            double m = margins_val.as_double();
            margins_ = {m, m, m, m};
        }
    } catch (const std::exception& e) {
        spdlog::error("Unable to parse margins: {}", e.what());
        return;
    }

    CTM ctm_obj;

    if (format_ == OutputFormat::Tactile) {
        double total_width = width + margins_[0] + margins_[2];
        double total_height = height + margins_[1] + margins_[3];
        double diagram_aspect = total_width / total_height;
        double page_aspect = 10.5 / 8.8;  // area available for graphics

        double s, lly;
        if (diagram_aspect >= page_aspect) {
            s = 756.0 / total_width;
            lly = s * total_height + 79.2;
            centerline_ = 378.0 + 36.0;
        } else {
            s = 633.6 / total_height;
            lly = 712.8;
            centerline_ = s * total_width / 2.0 + 36.0;
        }
        bottomline_ = lly;
        ctm_obj.translate(36.0, lly);
        ctm_obj.scale(s, -s);
        ctm_obj.translate(margins_[0], margins_[1]);
        root_.append_attribute("width").set_value("828");
        root_.append_attribute("height").set_value("792");
    } else {
        double w = width + margins_[0] + margins_[2];
        double h = height + margins_[1] + margins_[3];
        root_.append_attribute("width").set_value(std::to_string(w).c_str());
        root_.append_attribute("height").set_value(std::to_string(h).c_str());
        std::string viewBox = std::format("0 0 {} {}", w, h);
        root_.append_attribute("viewBox").set_value(viewBox.c_str());

        ctm_obj.translate(0, height + margins_[1] + margins_[3]);
        ctm_obj.scale(1, -1);
        ctm_obj.translate(margins_[0], margins_[1]);
    }

    BBox bbox_val = {0, 0, width, height};

    // Enter bbox into namespace
    Eigen::VectorXd bbox_vec(4);
    bbox_vec << 0, 0, width, height;
    expr_ctx_.enter_namespace("bbox", Value(bbox_vec));

    ctm_stack_.clear();
    ctm_stack_.push_back({ctm_obj, bbox_val});
    scale_stack_ = {{"linear", "linear"}};

    // Initialize the SVG 'defs' element
    defs_ = root_.append_child("defs");

    // Create initial clippath
    auto clippath = scratch_doc_.append_child("clipPath");
    auto rect = clippath.append_child("rect");
    rect.append_attribute("x").set_value(float2str(margins_[0]).c_str());
    rect.append_attribute("y").set_value(float2str(margins_[3]).c_str());
    rect.append_attribute("width").set_value(float2str(width).c_str());
    rect.append_attribute("height").set_value(float2str(height).c_str());

    push_clippath(clippath);
}

void Diagram::parse(std::optional<XmlNode> element_opt,
                    std::optional<XmlNode> root_opt,
                    OutlineStatus outline_status) {
    XmlNode element = element_opt.value_or(diagram_element_);
    XmlNode root = root_opt.value_or(root_);

    // Strip namespace from element tag (pugixml stores local name directly normally,
    // but we handle it just in case)
    // In pugixml, element.name() already gives local name unless namespace prefix is used

    std::string prefix = to_string(format_) + "-";

    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        // Skip comments
        if (child.type() == pugi::node_comment) {
            continue;
        }
        // Skip non-element nodes (e.g., text nodes)
        if (child.type() != pugi::node_element) {
            continue;
        }

        // Strip namespace prefix from tag if present
        std::string tag = child.name();
        auto colon_pos = tag.find(':');
        if (colon_pos != std::string::npos) {
            tag = tag.substr(colon_pos + 1);
            child.set_name(tag.c_str());
        }

        // Convert @at to @id
        auto at_attr = child.attribute("at");
        if (at_attr) {
            if (!child.attribute("id")) {
                child.append_attribute("id").set_value(at_attr.value());
            } else {
                child.attribute("id").set_value(at_attr.value());
            }
        }

        // Validate EPUB IDs
        auto id_attr = child.attribute("id");
        if (id_attr) {
            std::string child_id = id_attr.value();
            if (!std::regex_match(child_id, epub_id_check)) {
                spdlog::error("The id {} has characters disallowed by EPUB", child_id);
                spdlog::error("  We will substitute disallowed characters to make the id EPUB compliant");
                spdlog::error("  Search for EPUB in the PreFigure documentation at https://prefigure.org");
                id_attr.set_value(epub_clean(child_id).c_str());
            }
        }

        // Apply defaults from publication file
        auto def_it = defaults_.find(tag);
        if (def_it != defaults_.end()) {
            for (auto attr = def_it->second.first_attribute(); attr; attr = attr.next_attribute()) {
                if (!child.attribute(attr.name())) {
                    child.append_attribute(attr.name()).set_value(attr.value());
                }
            }
        }

        // Replace format-specific attributes
        // Collect the format-specific attributes first to avoid modifying while iterating
        std::vector<std::pair<std::string, std::string>> format_attrs;
        for (auto attr = child.first_attribute(); attr; attr = attr.next_attribute()) {
            std::string attr_name = attr.name();
            if (attr_name.starts_with(prefix)) {
                format_attrs.emplace_back(attr_name.substr(prefix.size()), attr.value());
            }
        }
        for (auto& [attr_name, attr_val] : format_attrs) {
            if (child.attribute(attr_name.c_str())) {
                child.attribute(attr_name.c_str()).set_value(attr_val.c_str());
            } else {
                child.append_attribute(attr_name.c_str()).set_value(attr_val.c_str());
            }
        }

        // Dispatch to tag handler
        try {
            tags::parse_element(child, *this, root, outline_status);
        } catch (const std::exception& e) {
            spdlog::error("Error in parsing element {}", tag);
            spdlog::error("{}", e.what());
            continue;  // Continue with next sibling, don't abort entire parse
        }

        // Handle annotate='yes'
        std::string annotate = get_attr(child, "annotate", "no");
        if (annotate == "yes" && outline_status != OutlineStatus::AddOutline) {
            if (tag != "group" && tag != "repeat") {
                auto annotation = scratch_doc_.append_child("annotation");
                // Copy relevant attributes
                const char* attribs[] = {"id", "text", "sonify", "circular", "speech"};
                for (const char* a : attribs) {
                    auto attr = child.attribute(a);
                    if (attr) {
                        annotation.append_attribute(a).set_value(attr.value());
                    }
                }
                // Evaluate text and speech
                auto text_attr = annotation.attribute("text");
                if (text_attr) {
                    text_attr.set_value(evaluate_text(text_attr.value(), expr_ctx_).c_str());
                }
                auto speech_attr = annotation.attribute("speech");
                if (speech_attr) {
                    speech_attr.set_value(evaluate_text(speech_attr.value(), expr_ctx_).c_str());
                }
                add_annotation_to_branch(annotation);
            }
        }
    }
}

void Diagram::place_labels() {
    prefigure::place_labels(*this, filename_, root_, label_group_dict_);

    // Position any registered legends.  This must run AFTER the labels above,
    // because each legend needs the rendered dimensions of its item labels
    // (computed inside place_labels) to lay itself out.  Mirrors the
    // `for legend in self.legends: legend.place_legend(self)` loop in the
    // Python label.place_labels() function.
    for (auto& legend : legends_) {
        legend->place_legend(*this);
    }
}

void Diagram::end_figure() {
    // Form output filenames
    std::string suffix;
    if (diagram_number_.has_value()) {
        suffix = "-" + std::to_string(diagram_number_.value());
    }

    std::filesystem::path input_path(filename_);
    std::string basename = input_path.stem().string() + suffix;
    std::filesystem::path output_dir = input_path.parent_path() / "output";
    std::filesystem::path out = output_dir / basename;

    try {
        if (!std::filesystem::exists(output_dir)) {
            std::filesystem::create_directory(output_dir);
        }
        std::string svg_path = out.string() + ".svg";

        // Tag the document with a backend-identification comment so the
        // user (and downstream tooling) can tell at a glance whether an
        // output SVG came from the C++ pipeline or the Python fallback.
        // The Python pipeline does not emit any such comment, so the
        // presence/absence of "PreFigure C++ backend" is a reliable marker
        // when both backends write to the same `output/` directory:
        //   head -1 output/foo.svg                 # quick visual check
        //   grep -l 'PreFigure C++ backend' *.svg  # programmatic check
        // The comment is inserted as the first child of the document so it
        // shows up on the line after the (suppressed) XML declaration and
        // does not affect any element parsing or layout.
        if (!svg_doc_.first_child() ||
            svg_doc_.first_child().type() != pugi::node_comment) {
            auto marker = svg_doc_.prepend_child(pugi::node_comment);
            marker.set_value(" Generated by PreFigure C++ backend ");
        }

        if (!svg_doc_.save_file(svg_path.c_str(), "  ", pugi::format_indent | pugi::format_no_declaration)) {
            spdlog::error("Unable to write SVG at {}", svg_path);
            return;
        }
    } catch (const std::exception& e) {
        spdlog::error("Unable to write SVG at {}.svg: {}", out.string(), e.what());
        return;
    }

    // Write diagcess version if author annotations are present
    if (author_annotations_present_ && environment_ == Environment::Pretext) {
        // Remove height and width for diagcess version
        root_.remove_attribute("height");
        root_.remove_attribute("width");
        try {
            std::string diagcess_path = out.string() + "-diagcess.svg";
            if (!svg_doc_.save_file(diagcess_path.c_str(), "  ", pugi::format_indent | pugi::format_no_declaration)) {
                spdlog::error("Unable to write SVG at {}", diagcess_path);
                return;
            }
        } catch (const std::exception& e) {
            spdlog::error("Unable to write SVG at {}-diagcess.svg: {}", out.string(), e.what());
            return;
        }
    }

    // Write annotations
    if (annotations_root_) {
        pugi::xml_document ann_doc;
        auto diagram_node = ann_doc.append_child("diagram");
        diagram_node.append_copy(annotations_root_);

        std::string output_file;
        if (environment_ == Environment::Pretext) {
            output_file = out.string() + "-annotations.xml";
        } else {
            output_file = out.string() + ".xml";
        }
        try {
            if (!ann_doc.save_file(output_file.c_str(), "  ", pugi::format_indent)) {
                spdlog::error("Unable to write annotations in {}", output_file);
            }
        } catch (const std::exception& e) {
            spdlog::error("Unable to write annotations in {}: {}", output_file, e.what());
        }
    } else {
        // Try to remove old annotation files
        std::error_code ec;
        std::filesystem::remove(out.string() + ".xml", ec);
        std::filesystem::remove(out.string() + "-annotations.xml", ec);
    }
}

std::pair<std::string, std::optional<std::string>> Diagram::end_figure_to_string() {
    // Serialize SVG to string
    std::ostringstream svg_stream;
    svg_doc_.save(svg_stream, "", pugi::format_raw);
    std::string svg_string = svg_stream.str();

    std::optional<std::string> annotation_string;
    if (annotations_root_) {
        pugi::xml_document ann_doc;
        auto diagram_node = ann_doc.append_child("diagram");
        diagram_node.append_copy(annotations_root_);
        std::ostringstream ann_stream;
        ann_doc.save(ann_stream, "", pugi::format_raw);
        annotation_string = ann_stream.str();
    }

    return {svg_string, annotation_string};
}

void Diagram::annotate_source() {
    // Stub: will be fully implemented when annotations module is complete
}

CTM& Diagram::ctm() {
    return ctm_stack_.back().first;
}

BBox Diagram::bbox() {
    return ctm_stack_.back().second;
}

std::pair<CTM, BBox> Diagram::ctm_bbox() {
    return ctm_stack_.back();
}

void Diagram::push_ctm(std::pair<CTM, BBox> ctm_bbox_val) {
    ctm_stack_.push_back(std::move(ctm_bbox_val));
}

std::pair<CTM, BBox> Diagram::pop_ctm() {
    if (ctm_stack_.empty()) {
        throw std::runtime_error("CTM stack is empty");
    }
    auto result = std::move(ctm_stack_.back());
    ctm_stack_.pop_back();
    return result;
}

Point2d Diagram::transform(const Point2d& p) {
    auto& [ctm_ref, bbox_ref] = ctm_stack_.back();
    try {
        return ctm_ref.transform(p);
    } catch (...) {
        spdlog::error("Unable to apply coordinate transform to ({}, {})", p[0], p[1]);
        return Point2d(0, 0);
    }
}

Point2d Diagram::inverse_transform(const Point2d& p) {
    auto& [ctm_ref, bbox_ref] = ctm_stack_.back();
    try {
        return ctm_ref.inverse_transform(p);
    } catch (...) {
        spdlog::error("Unable to apply inverse coordinate transform to ({}, {})", p[0], p[1]);
        return Point2d(0, 0);
    }
}

void Diagram::add_id(XmlNode element, const std::string& id) {
    std::string result_id = find_id(element, id);
    if (element.attribute("id")) {
        element.attribute("id").set_value(result_id.c_str());
    } else {
        element.append_attribute("id").set_value(result_id.c_str());
    }
}

std::string Diagram::find_id(XmlNode element, const std::string& id) {
    std::string suffix;
    for (auto& s : id_suffix_) {
        suffix += s;
    }

    std::string result_id;
    if (id.empty()) {
        std::string tag = element.name();
        // Python: self.ids.get(element.tag, -1) + 1
        // Map default-inits to 0; we want first call to yield 0, so use post-increment
        auto it = ids_.find(tag);
        int count;
        if (it == ids_.end()) {
            ids_[tag] = 0;
            count = 0;
        } else {
            it->second += 1;
            count = it->second;
        }
        result_id = tag + "-" + std::to_string(count) + suffix;
    } else {
        result_id = id + suffix;
    }
    result_id = prepend_id_prefix(result_id);
    return result_id;
}

std::string Diagram::append_id_suffix(XmlNode element) {
    auto id_attr = element.attribute("id");
    std::string id = id_attr ? id_attr.value() : "";
    return find_id(element, id);
}

std::string Diagram::prepend_id_prefix(const std::string& id) {
    if (!add_id_prefix_) {
        return id;
    }
    if (id.starts_with(id_prefix_)) {
        return id;
    }
    return id_prefix_ + id;
}

void Diagram::push_clippath(XmlNode clippath) {
    // Append the clippath to defs
    auto cp = defs_.append_copy(clippath);
    add_id(cp);
    std::string cp_id = cp.attribute("id").value();
    clippaths_.push_back(cp_id);
}

void Diagram::pop_clippath() {
    if (!clippaths_.empty()) {
        clippaths_.pop_back();
    }
}

std::string Diagram::get_clippath() {
    if (clippaths_.empty()) {
        return "";
    }
    return clippaths_.back();
}

void Diagram::push_scales(std::array<std::string, 2> scales) {
    scale_stack_.push_back(std::move(scales));
}

void Diagram::pop_scales() {
    if (!scale_stack_.empty()) {
        scale_stack_.pop_back();
    }
}

std::array<std::string, 2> Diagram::get_scales() {
    if (scale_stack_.empty()) {
        return {"linear", "linear"};
    }
    return scale_stack_.back();
}

XmlNode Diagram::get_root() {
    return root_;
}

XmlNode Diagram::get_defs() {
    return defs_;
}

XmlNode Diagram::get_scratch() {
    // Lazily create a persistent root node in the scratch document
    XmlNode root = scratch_doc_.first_child();
    if (!root) {
        root = scratch_doc_.append_child("scratch");
    }
    return root;
}

void Diagram::add_reusable(XmlNode element) {
    auto id_attr = element.attribute("id");
    std::string id = id_attr ? id_attr.value() : "none";
    if (has_reusable(id)) {
        return;
    }
    defs_.append_copy(element);
    reusables_[id] = element;
}

bool Diagram::has_reusable(const std::string& id) {
    return reusables_.count(id) > 0;
}

XmlNode Diagram::get_reusable(const std::string& id) {
    auto it = reusables_.find(id);
    if (it != reusables_.end()) {
        return it->second;
    }
    return XmlNode();
}

OutputFormat Diagram::output_format() const {
    return format_;
}

void Diagram::set_output_format(OutputFormat fmt) {
    format_ = fmt;
}

Environment Diagram::get_environment() const {
    return environment_;
}

void Diagram::register_svg_element(XmlNode source, XmlNode svg, bool overwrite) {
    size_t key = source.hash_value();
    if (overwrite || source_to_svg_map_.find(key) == source_to_svg_map_.end()) {
        source_to_svg_map_[key] = svg;
    }
}

void Diagram::add_label(XmlNode element, XmlNode group) {
    size_t key = element.hash_value();
    label_group_dict_[key] = std::make_tuple(element, group, ctm().copy());
}

void Diagram::register_label_dims(XmlNode element, std::pair<double, double> dims) {
    label_dims_[element.hash_value()] = dims;
}

std::pair<double, double> Diagram::get_label_dims(XmlNode element) {
    auto it = label_dims_.find(element.hash_value());
    if (it == label_dims_.end()) {
        spdlog::error("Cannot find dimensions for a label");
        return {0, 0};
    }
    return it->second;
}

void Diagram::add_legend(std::unique_ptr<Legend> legend) {
    legends_.push_back(std::move(legend));
}

// Out-of-line destructor — required because legends_ holds
// std::unique_ptr<Legend>, and unique_ptr's destructor needs Legend to be
// a complete type.  legend.hpp is included at the top of this TU, so by the
// time the compiler instantiates ~Diagram() here, Legend is fully visible.
Diagram::~Diagram() = default;

std::tuple<XmlNode, XmlNode, CTM> Diagram::get_label_group(XmlNode element) {
    auto it = label_group_dict_.find(element.hash_value());
    if (it != label_group_dict_.end()) {
        return it->second;
    }
    return {XmlNode(), XmlNode(), CTM()};
}

std::unordered_map<size_t, std::tuple<XmlNode, XmlNode, CTM>>& Diagram::get_label_group_dict() {
    return label_group_dict_;
}

const std::unordered_map<size_t, XmlNode>& Diagram::get_source_to_svg_map() const {
    return source_to_svg_map_;
}

void Diagram::add_shape(XmlNode shape_node) {
    defs_.append_copy(shape_node);
    auto id_attr = shape_node.attribute("id");
    if (!id_attr) {
        id_attr = shape_node.attribute("at");
    }
    if (id_attr) {
        shape_dict_[id_attr.value()] = shape_node;
    }
}

XmlNode Diagram::recall_shape(const std::string& id) {
    auto it = shape_dict_.find(id);
    if (it != shape_dict_.end()) {
        return it->second;
    }
    return XmlNode();
}

XmlNode Diagram::get_shape(const std::string& id) {
    auto shape = recall_shape(id);
    if (shape) {
        return shape;
    }

    // Search for path elements in root
    for (auto child = root_.first_child(); child; child = child.next_sibling()) {
        if (std::string(child.name()) == "path") {
            auto child_id = child.attribute("id");
            if (child_id && std::string(child_id.value()) == id) {
                return child;
            }
        }
    }

    spdlog::error("We cannot find a <shape> with id={}", id);
    return XmlNode();
}

void Diagram::apply_defaults(const std::string& tag, XmlNode element) {
    auto it = defaults_.find(tag);
    if (it == defaults_.end()) return;
    for (auto attr = it->second.first_attribute(); attr; attr = attr.next_attribute()) {
        if (!element.attribute(attr.name())) {
            element.append_attribute(attr.name()).set_value(attr.value());
        }
    }
}

std::string Diagram::get_external() {
    return external_;
}

std::array<double, 4> Diagram::get_margins() {
    return margins_;
}

void Diagram::add_outline(XmlNode element, XmlNode path, XmlNode parent, int outline_width) {
    if (outline_width < 0) {
        if (format_ == OutputFormat::Tactile) {
            outline_width = 18;
        } else {
            outline_width = 4;
        }
    }

    // Extract and remove styling attributes from path
    std::string stroke = "none";
    std::string width_str = "1";
    std::string fill = "none";

    auto stroke_attr = path.attribute("stroke");
    if (stroke_attr) { stroke = stroke_attr.value(); path.remove_attribute("stroke"); }
    auto width_attr = path.attribute("stroke-width");
    if (width_attr) { width_str = width_attr.value(); path.remove_attribute("stroke-width"); }
    auto fill_attr = path.attribute("fill");
    if (fill_attr) { fill = fill_attr.value(); path.remove_attribute("fill"); }
    path.remove_attribute("stroke-dasharray");

    // Set up the id for the outline
    add_id(element, get_attr(element, "id", ""));
    std::string outline_id = std::string(element.attribute("id").value()) + "-outline";
    if (path.attribute("id")) {
        path.attribute("id").set_value(outline_id.c_str());
    } else {
        path.append_attribute("id").set_value(outline_id.c_str());
    }
    add_reusable(path);

    // Create <use> element with white outline stroke
    auto use = parent.append_child("use");
    use.append_attribute("fill").set_value(fill.c_str());

    int total_width = 0;
    try {
        total_width = std::stoi(width_str) + outline_width;
    } catch (...) {
        total_width = 1 + outline_width;
    }
    use.append_attribute("stroke-width").set_value(std::to_string(total_width).c_str());
    use.append_attribute("stroke").set_value("white");
    use.append_attribute("href").set_value(("#" + outline_id).c_str());

    // Handle arrow marker references
    const char* markers[] = {"marker-end", "marker-start", "marker-mid"};
    for (const char* marker : markers) {
        auto ref_attr = path.attribute(marker);
        if (ref_attr) {
            std::string reference = ref_attr.value();
            // Replace closing ) with -outline)
            auto pos = reference.rfind(')');
            if (pos != std::string::npos) {
                reference = reference.substr(0, pos) + "-outline)";
            }
            use.append_attribute(marker).set_value(reference.c_str());
        }
    }
}

void Diagram::finish_outline(XmlNode element, const std::string& stroke,
                             const std::string& thickness, const std::string& fill, XmlNode parent) {
    auto use = parent.append_child("use");

    std::string elem_id = get_attr(element, "id", "none");
    use.append_attribute("id").set_value(elem_id.c_str());
    use.append_attribute("fill").set_value(fill.c_str());
    use.append_attribute("stroke-width").set_value(thickness.c_str());
    use.append_attribute("stroke").set_value(stroke.c_str());
    use.append_attribute("stroke-dasharray").set_value(get_attr(element, "dash", "none").c_str());

    register_svg_element(element, use);

    // Remove duplicate id if parent has same id
    std::string parent_id = get_attr(parent, "id", "none");
    if (elem_id == parent_id) {
        use.remove_attribute("id");
    }

    // Clean up arrow head references
    std::string element_id = get_attr(element, "id", "");
    std::string last_suffix = id_suffix_.back();
    std::string reuse_handle;
    if (element_id.ends_with(last_suffix)) {
        reuse_handle = element_id + "-outline";
    } else {
        reuse_handle = element_id + last_suffix + "-outline";
    }

    auto reusable = get_reusable(reuse_handle);
    use.append_attribute("href").set_value(("#" + reuse_handle).c_str());

    if (reusable) {
        const char* markers[] = {"marker-start", "marker-end", "marker-mid"};
        for (const char* marker : markers) {
            auto marker_attr = reusable.attribute(marker);
            if (marker_attr && std::string(marker_attr.value()) != "none") {
                use.append_attribute(marker).set_value(marker_attr.value());
                reusable.remove_attribute(marker);
            }
        }
    }
}

void Diagram::initialize_annotations() {
    if (annotations_root_) {
        spdlog::error("Annotations need to be in a single tree");
        return;
    }
    annotations_root_ = scratch_doc_.append_child("annotations");
}

void Diagram::add_default_annotation(XmlNode annotation) {
    if (!add_default_annotations_) {
        return;
    }
    default_annotations_.push_back(annotation);
}

std::vector<XmlNode>& Diagram::get_default_annotations() {
    return default_annotations_;
}

XmlNode Diagram::get_annotations_root() {
    return annotations_root_;
}

void Diagram::add_annotation(XmlNode annotation) {
    if (annotations_root_) {
        annotations_root_.append_copy(annotation);
    }
}

void Diagram::push_to_annotation_branch(XmlNode annotation) {
    if (annotation_branch_stack_.empty()) {
        auto id_attr = annotation.attribute("id");
        if (id_attr) {
            annotation_branches_[id_attr.value()] = annotation;
        }
    } else {
        add_annotation_to_branch(annotation);
    }
    annotation_branch_stack_.push_back(annotation);
}

void Diagram::pop_from_annotation_branch() {
    if (!annotation_branch_stack_.empty()) {
        annotation_branch_stack_.pop_back();
    }
}

void Diagram::add_annotation_to_branch(XmlNode annotation) {
    if (annotation_branch_stack_.empty()) {
        auto id_attr = annotation.attribute("id");
        if (id_attr) {
            std::string id = prepend_id_prefix(id_attr.value());
            annotation_branches_[id] = annotation;
        }
        return;
    }
    annotation_branch_stack_.back().append_copy(annotation);
    std::string id = append_id_suffix(annotation);
    id = prepend_id_prefix(id);
    if (annotation.attribute("id")) {
        annotation.attribute("id").set_value(id.c_str());
    } else {
        annotation.append_attribute("id").set_value(id.c_str());
    }
}

XmlNode Diagram::get_annotation_branch(const std::string& id) {
    auto it = annotation_branches_.find(id);
    if (it != annotation_branches_.end()) {
        auto result = it->second;
        annotation_branches_.erase(it);
        return result;
    }
    return XmlNode();
}

void Diagram::push_id_suffix(const std::string& suffix) {
    id_suffix_.push_back(suffix);
}

void Diagram::pop_id_suffix() {
    if (!id_suffix_.empty()) {
        id_suffix_.pop_back();
    }
}

void Diagram::register_source_data(XmlNode element, const std::string& key, const Value& value) {
    source_data_[element.hash_value()][key] = value;
}

Value Diagram::get_source_data(XmlNode element, const std::string& key) {
    auto it = source_data_.find(element.hash_value());
    if (it == source_data_.end()) return Value();
    auto kit = it->second.find(key);
    if (kit == it->second.end()) return Value();
    return kit->second;
}

void Diagram::save_data(XmlNode element, const Value& data) {
    saved_data_[element.hash_value()] = data;
}

Value Diagram::retrieve_data(XmlNode element) {
    auto it = saved_data_.find(element.hash_value());
    if (it != saved_data_.end()) {
        return it->second;
    }
    return Value();
}

void Diagram::save_network_coordinates(const std::string& network_name, const Value& coordinates) {
    network_coords_[network_name] = coordinates;
}

Value Diagram::get_network_coordinates(const std::string& network_name) {
    auto it = network_coords_.find(network_name);
    if (it != network_coords_.end()) {
        return it->second;
    }
    return Value();
}

ExpressionContext& Diagram::expr_ctx() {
    return expr_ctx_;
}

XmlNode Diagram::get_diagram_element() const {
    return diagram_element_;
}

void Diagram::set_caption(const std::string& text) {
    caption_ = text;
}

bool Diagram::caption_suppressed() const {
    return suppress_caption_;
}

}  // namespace prefigure
