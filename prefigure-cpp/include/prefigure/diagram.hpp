#pragma once

#include "types.hpp"
#include "ctm.hpp"
#include "user_namespace.hpp"

#include <array>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace prefigure {

class Diagram {
public:
    Diagram(XmlNode diagram_element, const std::string& filename,
            std::optional<int> diagram_number, OutputFormat format,
            std::optional<std::string> output, XmlNode publication,
            bool suppress_caption, Environment environment);

    void begin_figure();
    void parse(std::optional<XmlNode> element = std::nullopt,
               std::optional<XmlNode> root = std::nullopt,
               OutlineStatus outline_status = OutlineStatus::None);
    void place_labels();
    void end_figure();
    std::pair<std::string, std::optional<std::string>> end_figure_to_string();
    void annotate_source();

    CTM& ctm();
    BBox bbox();
    void push_ctm(std::pair<CTM, BBox> ctm_bbox);
    std::pair<CTM, BBox> pop_ctm();

    Point2d transform(const Point2d& p);
    Point2d inverse_transform(const Point2d& p);

    void add_id(XmlNode element, const std::string& id = "");
    std::string find_id(XmlNode element, const std::string& id = "");
    std::string prepend_id_prefix(const std::string& id);

    void push_clippath(XmlNode clippath);
    void pop_clippath();
    std::string get_clippath();

    void push_scales(std::array<std::string, 2> scales);
    void pop_scales();
    std::array<std::string, 2> get_scales();

    XmlNode get_root();
    XmlNode get_defs();
    void add_reusable(XmlNode element);
    bool has_reusable(const std::string& id);
    XmlNode get_reusable(const std::string& id);

    OutputFormat output_format() const;
    Environment get_environment() const;

    void register_svg_element(XmlNode source, XmlNode svg, bool overwrite = true);
    void add_label(XmlNode element, XmlNode group);
    void register_label_dims(XmlNode element, std::pair<double, double> dims);
    void add_legend(/* Legend& */);
    void add_shape(XmlNode shape_node);
    XmlNode recall_shape(const std::string& id);
    XmlNode get_shape(const std::string& id);
    void apply_defaults(const std::string& tag, XmlNode element);
    std::string get_external();
    std::array<double, 4> get_margins();

    void add_outline(XmlNode element, XmlNode path, XmlNode parent, int outline_width = -1);
    void finish_outline(XmlNode element, const std::string& stroke,
                       const std::string& thickness, const std::string& fill, XmlNode parent);

    // Annotations
    void initialize_annotations();
    void add_annotation(XmlNode annotation);
    void add_default_annotation(XmlNode annotation);
    void push_to_annotation_branch(XmlNode annotation);
    void pop_from_annotation_branch();
    void add_annotation_to_branch(XmlNode annotation);

    // Data storage
    void register_source_data(XmlNode element, const std::string& key, const Value& value);
    Value get_source_data(XmlNode element, const std::string& key);
    void save_data(XmlNode element, const Value& data);
    Value retrieve_data(XmlNode element);

    // Network
    void save_network_coordinates(const std::string& network_name, const Value& coordinates);
    Value get_network_coordinates(const std::string& network_name);

    // Expression context
    ExpressionContext& expr_ctx();

private:
    // Document objects
    XmlDoc svg_doc_;
    XmlNode diagram_element_;
    XmlNode root_;
    XmlNode defs_;

    // Configuration
    std::string filename_;
    std::optional<int> diagram_number_;
    OutputFormat format_;
    std::optional<std::string> output_;
    XmlNode publication_;
    bool suppress_caption_;
    Environment environment_;

    // CTM stack
    std::vector<std::pair<CTM, BBox>> ctm_stack_;

    // Clippath stack
    std::vector<XmlNode> clippath_stack_;

    // Scales stack
    std::vector<std::array<std::string, 2>> scales_stack_;

    // ID management
    std::map<std::string, XmlNode> id_map_;
    std::string id_prefix_;

    // Reusable elements (defs)
    std::map<std::string, XmlNode> reusable_map_;

    // Shapes
    std::map<std::string, XmlNode> shape_map_;

    // Data storage
    std::map<std::string, Value> data_map_;
    std::map<std::string, std::map<std::string, Value>> source_data_map_;

    // Network coordinates
    std::map<std::string, Value> network_coords_;

    // Labels
    std::vector<std::pair<XmlNode, XmlNode>> labels_;

    // Annotations
    std::vector<XmlNode> annotation_branch_;
};

}  // namespace prefigure
