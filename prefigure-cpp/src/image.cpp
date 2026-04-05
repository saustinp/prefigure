#include "prefigure/image.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/group.hpp"
#include "prefigure/utilities.hpp"

#include <base64.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace prefigure {

static const std::unordered_map<std::string, std::string> type_dict = {
    {"jpg", "jpeg"}, {"jpeg", "jpeg"}, {"png", "png"},
    {"gif", "gif"}, {"svg", "svg"}
};

void image(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    // Check that the image has children (for tactile fallback)
    bool has_children = false;
    for (auto child = element.first_child(); child; child = child.next_sibling()) {
        if (child.type() == pugi::node_element) {
            has_children = true;
            break;
        }
    }
    if (!has_children) {
        spdlog::error("An <image> must contain content to replace the image in a tactile build");
        return;
    }

    // In tactile mode, render children only
    if (diagram.output_format() == OutputFormat::Tactile) {
        element.set_name("group");
        group(element, diagram, parent, status);
        return;
    }

    auto source_attr = element.attribute("source");
    if (!source_attr) {
        spdlog::error("An <image> needs a @source attribute");
        return;
    }
    std::string source = source_attr.value();

    // Parse placement data
    Point2d ll, dims, center_pt;
    double rotation = 0.0;
    double scale_val = 1.0;
    bool has_center = false;
    bool scale_is_vector = false;
    Point2d scale_vec;

    try {
        auto ll_val = diagram.expr_ctx().eval(get_attr(element, "lower-left", "(0,0)"));
        ll = ll_val.as_point();

        auto dims_val = diagram.expr_ctx().eval(get_attr(element, "dimensions", "(1,1)"));
        dims = dims_val.as_point();

        auto center_attr = element.attribute("center");
        if (center_attr) {
            auto cv = diagram.expr_ctx().eval(center_attr.value());
            center_pt = cv.as_point();
            ll = center_pt - 0.5 * dims;
            has_center = true;
        } else {
            center_pt = ll + 0.5 * dims;
        }

        auto rot_val = diagram.expr_ctx().eval(get_attr(element, "rotate", "0"));
        rotation = rot_val.to_double();

        auto scale_result = diagram.expr_ctx().eval(get_attr(element, "scale", "1"));
        if (scale_result.is_vector()) {
            scale_is_vector = true;
            scale_vec = scale_result.as_point();
        } else {
            scale_val = scale_result.to_double();
        }
    } catch (...) {
        spdlog::error("Error parsing placement data in an <image>");
        return;
    }

    // Determine file type
    std::string file_type;
    auto ft_attr = element.attribute("filetype");
    if (ft_attr) {
        auto it = type_dict.find(ft_attr.value());
        if (it != type_dict.end()) file_type = it->second;
    }
    if (file_type.empty()) {
        std::string suffix = source.substr(source.rfind('.') + 1);
        auto it = type_dict.find(suffix);
        if (it != type_dict.end()) file_type = it->second;
    }
    if (file_type.empty()) {
        spdlog::error("Cannot determine the type of image in {}", source);
        return;
    }

    // Transform coordinates
    Point2d ll_svg = diagram.transform(ll);
    Point2d ur_svg = diagram.transform(ll + dims);
    Point2d center_svg = diagram.transform(center_pt);
    double width = ur_svg[0] - ll_svg[0];
    double height = -(ur_svg[1] - ll_svg[1]);

    // Resolve source path
    if (diagram.get_environment() == Environment::Pretext) {
        source = "data/" + source;
    } else {
        std::string assets_dir = diagram.get_external();
        if (!assets_dir.empty()) {
            // Trim
            auto start = assets_dir.find_first_not_of(" \t");
            auto end = assets_dir.find_last_not_of(" \t");
            if (start != std::string::npos) {
                assets_dir = assets_dir.substr(start, end - start + 1);
            }
            if (assets_dir.back() != '/') assets_dir += '/';
            source = assets_dir + source;
        }
    }

    // Parse opacity
    double opacity = -1.0;
    auto opacity_attr = element.attribute("opacity");
    if (opacity_attr) {
        opacity = diagram.expr_ctx().eval(opacity_attr.value()).to_double();
    }

    // Handle SVG embedded images
    if (file_type == "svg") {
        pugi::xml_document svg_doc;
        auto result = svg_doc.load_file(source.c_str());
        if (!result) {
            spdlog::error("Failed to parse SVG file: {}", source);
            return;
        }
        auto svg_root = svg_doc.first_child();
        std::string svg_width_str = svg_root.attribute("width") ?
            svg_root.attribute("width").value() : "";
        std::string svg_height_str = svg_root.attribute("height") ?
            svg_root.attribute("height").value() : "";

        auto foreign = parent.append_child("foreignObject");
        foreign.append_attribute("x").set_value(
            float2str(center_svg[0] - width / 2.0).c_str());
        foreign.append_attribute("y").set_value(
            float2str(center_svg[1] - height / 2.0).c_str());
        foreign.append_attribute("width").set_value(float2str(width).c_str());
        foreign.append_attribute("height").set_value(float2str(height).c_str());

        // Copy SVG root into foreignObject
        auto embedded = foreign.append_copy(svg_root);
        embedded.attribute("width") ?
            embedded.attribute("width").set_value("100%") :
            embedded.append_attribute("width").set_value("100%");
        embedded.attribute("height") ?
            embedded.attribute("height").set_value("100%") :
            embedded.append_attribute("height").set_value("100%");

        if (!embedded.attribute("viewBox") && !svg_width_str.empty() && !svg_height_str.empty()) {
            std::string vb = "0 0 " + svg_width_str + " " + svg_height_str;
            embedded.append_attribute("viewBox").set_value(vb.c_str());
        }

        diagram.add_id(foreign, get_attr(element, "id", ""));
        embedded.remove_attribute("id");

        if (opacity >= 0) {
            foreign.append_attribute("opacity").set_value(float2str(opacity).c_str());
        }
        return;
    }

    // Raster image: base64 encode
    auto image_el = parent.append_child("image");
    diagram.register_svg_element(element, image_el);
    image_el.append_attribute("x").set_value(float2str(-width / 2.0).c_str());
    image_el.append_attribute("y").set_value(float2str(-height / 2.0).c_str());
    image_el.append_attribute("width").set_value(float2str(width).c_str());
    image_el.append_attribute("height").set_value(float2str(height).c_str());
    if (opacity >= 0) {
        image_el.append_attribute("opacity").set_value(float2str(opacity).c_str());
    }
    diagram.add_id(image_el, get_attr(element, "id", ""));

    // Read and base64 encode the image file
    std::ifstream file(source, std::ios::binary);
    if (!file) {
        spdlog::error("Cannot open image file: {}", source);
        return;
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    std::string raw_data = oss.str();
    std::string encoded = base64_encode(
        reinterpret_cast<const unsigned char*>(raw_data.data()),
        raw_data.size());

    std::string href = "data:image/" + file_type + ";base64," + encoded;
    image_el.append_attribute("href").set_value(href.c_str());

    // Build transform
    std::string transform_str = translatestr(center_svg[0], center_svg[1]);

    if (scale_is_vector) {
        transform_str += " " + scalestr(scale_vec[0], scale_vec[1]);
    } else if (scale_val != 1.0) {
        transform_str += " scale(" + float2str(scale_val) + ")";
    }

    if (rotation != 0.0) {
        transform_str += " rotate(" + float2str(-rotation) + ")";
    }

    image_el.append_attribute("transform").set_value(transform_str.c_str());
}

}  // namespace prefigure
