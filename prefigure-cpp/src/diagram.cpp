#include "prefigure/diagram.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/user_namespace.hpp"

#include <stdexcept>

namespace prefigure {

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
}

void Diagram::begin_figure() {
    // TODO: implement
}

void Diagram::parse(std::optional<XmlNode> element,
                    std::optional<XmlNode> root,
                    OutlineStatus outline_status) {
    (void)element; (void)root; (void)outline_status;
}

void Diagram::place_labels() {
    // TODO: implement
}

void Diagram::end_figure() {
    // TODO: implement
}

std::pair<std::string, std::optional<std::string>> Diagram::end_figure_to_string() {
    return {"", std::nullopt};
}

void Diagram::annotate_source() {
    // TODO: implement
}

CTM& Diagram::ctm() {
    static CTM dummy;
    return dummy;
}

BBox Diagram::bbox() {
    return {0.0, 0.0, 0.0, 0.0};
}

void Diagram::push_ctm(std::pair<CTM, BBox> ctm_bbox) {
    ctm_stack_.push_back(std::move(ctm_bbox));
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
    (void)p;
    return Point2d(0.0, 0.0);
}

Point2d Diagram::inverse_transform(const Point2d& p) {
    (void)p;
    return Point2d(0.0, 0.0);
}

void Diagram::add_id(XmlNode element, const std::string& id) {
    (void)element; (void)id;
}

std::string Diagram::find_id(XmlNode element, const std::string& id) {
    (void)element; (void)id;
    return "";
}

std::string Diagram::prepend_id_prefix(const std::string& id) {
    return id_prefix_ + id;
}

void Diagram::push_clippath(XmlNode clippath) {
    clippath_stack_.push_back(clippath);
}

void Diagram::pop_clippath() {
    if (!clippath_stack_.empty()) {
        clippath_stack_.pop_back();
    }
}

std::string Diagram::get_clippath() {
    return "";
}

void Diagram::push_scales(std::array<std::string, 2> scales) {
    scales_stack_.push_back(std::move(scales));
}

void Diagram::pop_scales() {
    if (!scales_stack_.empty()) {
        scales_stack_.pop_back();
    }
}

std::array<std::string, 2> Diagram::get_scales() {
    if (scales_stack_.empty()) {
        return {"", ""};
    }
    return scales_stack_.back();
}

XmlNode Diagram::get_root() {
    return root_;
}

XmlNode Diagram::get_defs() {
    return defs_;
}

void Diagram::add_reusable(XmlNode element) {
    (void)element;
}

bool Diagram::has_reusable(const std::string& id) {
    return reusable_map_.count(id) > 0;
}

XmlNode Diagram::get_reusable(const std::string& id) {
    return reusable_map_.at(id);
}

OutputFormat Diagram::output_format() const {
    return format_;
}

Environment Diagram::get_environment() const {
    return environment_;
}

void Diagram::register_svg_element(XmlNode source, XmlNode svg, bool overwrite) {
    (void)source; (void)svg; (void)overwrite;
}

void Diagram::add_label(XmlNode element, XmlNode group_node) {
    labels_.emplace_back(element, group_node);
}

void Diagram::register_label_dims(XmlNode element, std::pair<double, double> dims) {
    (void)element; (void)dims;
}

void Diagram::add_legend(/* Legend& */) {
    // TODO: implement
}

void Diagram::add_shape(XmlNode shape_node) {
    (void)shape_node;
}

XmlNode Diagram::recall_shape(const std::string& id) {
    (void)id;
    return XmlNode();
}

XmlNode Diagram::get_shape(const std::string& id) {
    (void)id;
    return XmlNode();
}

void Diagram::apply_defaults(const std::string& tag, XmlNode element) {
    (void)tag; (void)element;
}

std::string Diagram::get_external() {
    return "";
}

std::array<double, 4> Diagram::get_margins() {
    return {0.0, 0.0, 0.0, 0.0};
}

void Diagram::add_outline(XmlNode element, XmlNode path, XmlNode parent, int outline_width) {
    (void)element; (void)path; (void)parent; (void)outline_width;
}

void Diagram::finish_outline(XmlNode element, const std::string& stroke,
                             const std::string& thickness, const std::string& fill, XmlNode parent) {
    (void)element; (void)stroke; (void)thickness; (void)fill; (void)parent;
}

void Diagram::initialize_annotations() {
    // TODO: implement
}

void Diagram::add_annotation(XmlNode annotation) {
    (void)annotation;
}

void Diagram::add_default_annotation(XmlNode annotation) {
    (void)annotation;
}

void Diagram::push_to_annotation_branch(XmlNode annotation) {
    annotation_branch_.push_back(annotation);
}

void Diagram::pop_from_annotation_branch() {
    if (!annotation_branch_.empty()) {
        annotation_branch_.pop_back();
    }
}

void Diagram::add_annotation_to_branch(XmlNode annotation) {
    (void)annotation;
}

void Diagram::register_source_data(XmlNode element, const std::string& key, const Value& value) {
    (void)element; (void)key; (void)value;
}

Value Diagram::get_source_data(XmlNode element, const std::string& key) {
    (void)element; (void)key;
    return Value();
}

void Diagram::save_data(XmlNode element, const Value& data) {
    (void)element; (void)data;
}

Value Diagram::retrieve_data(XmlNode element) {
    (void)element;
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
    static ExpressionContext dummy;
    return dummy;
}

}  // namespace prefigure
