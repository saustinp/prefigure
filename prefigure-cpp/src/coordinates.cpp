#include "prefigure/coordinates.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/utilities.hpp"
#include "prefigure/user_namespace.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <string>

namespace prefigure {

void coordinates(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus outline_status) {
    auto [current_ctm, current_bbox] = diagram.ctm_bbox();

    // Parse destination
    Eigen::VectorXd destination(4);
    auto dest_attr = element.attribute("destination");
    std::string destination_str;
    if (!dest_attr) {
        destination << current_bbox[0], current_bbox[1], current_bbox[2], current_bbox[3];
        destination_str = np2str(Point2d(destination[0], destination[1]));
    } else {
        destination_str = dest_attr.value();
        Value dest_val = diagram.expr_ctx().eval(destination_str);
        if (dest_val.is_vector()) {
            destination = dest_val.as_vector();
        }
    }

    Point2d dest_ll(destination[0], destination[1]);
    Point2d dest_ur(destination[2], destination[3]);
    Point2d lower_left_clip = diagram.transform(dest_ll);
    Point2d upper_right_clip = diagram.transform(dest_ur);

    double dest_dx = upper_right_clip[0] - lower_left_clip[0];
    double dest_dy = -(upper_right_clip[1] - lower_left_clip[1]);

    // Parse bbox
    auto bbox_attr = element.attribute("bbox");
    if (!bbox_attr) {
        spdlog::error("A <coordinates> element requires a @bbox attribute");
        return;
    }
    Value bbox_val = diagram.expr_ctx().eval(bbox_attr.value());
    Eigen::VectorXd bbox_vec;
    if (bbox_val.is_vector()) {
        bbox_vec = bbox_val.as_vector();
    } else {
        spdlog::error("Unable to parse bbox in <coordinates>");
        return;
    }

    // Handle aspect-ratio
    auto aspect_attr = element.attribute("aspect-ratio");
    if (aspect_attr) {
        double ratio = diagram.expr_ctx().eval(aspect_attr.value()).to_double();
        std::string preserve_y = get_attr(element, "preserve-y-range", "no");
        if (preserve_y == "yes") {
            double box_dy = bbox_vec[3] - bbox_vec[1];
            double y_scale = dest_dy / box_dy;
            double x_scale = ratio * y_scale;
            double box_dx = dest_dx / x_scale;
            bbox_vec[2] = bbox_vec[0] + box_dx;
            // bbox_vec[3] stays the same
        } else {
            double box_dx = bbox_vec[2] - bbox_vec[0];
            double x_scale = dest_dx / box_dx;
            double y_scale = x_scale / ratio;
            double box_dy = dest_dy / y_scale;
            bbox_vec[3] = bbox_vec[1] + box_dy;
        }
    }

    // Create clippath
    pugi::xml_document clip_doc;
    auto clippath = clip_doc.append_child("clipPath");
    auto clip_box = clippath.append_child("rect");
    clip_box.append_attribute("x").set_value(float2str(lower_left_clip[0]).c_str());
    clip_box.append_attribute("y").set_value(float2str(upper_right_clip[1]).c_str());
    double clip_width = upper_right_clip[0] - lower_left_clip[0];
    double clip_height = lower_left_clip[1] - upper_right_clip[1];
    clip_box.append_attribute("width").set_value(float2str(clip_width).c_str());
    clip_box.append_attribute("height").set_value(float2str(clip_height).c_str());
    diagram.push_clippath(clippath);

    // Parse scales
    std::string scales_str = get_attr(element, "scales", "linear");
    std::array<std::string, 2> scales;
    if (scales_str == "linear") {
        scales = {"linear", "linear"};
    } else if (scales_str == "semilogx") {
        scales = {"log", "linear"};
    } else if (scales_str == "semilogy") {
        scales = {"linear", "log"};
    } else if (scales_str == "loglog") {
        scales = {"log", "log"};
    } else {
        scales = {"linear", "linear"};
    }

    CTM new_ctm = current_ctm.copy();
    diagram.push_scales(scales);

    Eigen::VectorXd scaled_bbox = bbox_vec;
    if (scales[0] == "log") {
        scaled_bbox[0] = std::log10(scaled_bbox[0]);
        scaled_bbox[2] = std::log10(scaled_bbox[2]);
        new_ctm.set_log_x();
    }
    if (scales[1] == "log") {
        scaled_bbox[1] = std::log10(scaled_bbox[1]);
        scaled_bbox[3] = std::log10(scaled_bbox[3]);
        new_ctm.set_log_y();
    }

    new_ctm.translate(destination[0], destination[1]);
    new_ctm.scale(
        (destination[2] - destination[0]) / (scaled_bbox[2] - scaled_bbox[0]),
        (destination[3] - destination[1]) / (scaled_bbox[3] - scaled_bbox[1])
    );
    new_ctm.translate(-scaled_bbox[0], -scaled_bbox[1]);

    // Enter scaled bbox into namespace
    std::string bbox_str = "[" +
        std::to_string(scaled_bbox[0]) + "," +
        std::to_string(scaled_bbox[1]) + "," +
        std::to_string(scaled_bbox[2]) + "," +
        std::to_string(scaled_bbox[3]) + "]";
    diagram.expr_ctx().eval(bbox_str, std::string("bbox"));

    BBox new_bbox = {bbox_vec[0], bbox_vec[1], bbox_vec[2], bbox_vec[3]};
    diagram.push_ctm({new_ctm, new_bbox});
    diagram.parse(element, parent, outline_status);
    diagram.pop_ctm();
    diagram.pop_clippath();
    diagram.pop_scales();
}

}  // namespace prefigure
