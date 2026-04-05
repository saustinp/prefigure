#include "prefigure/statistics.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/math_utilities.hpp"
#include "prefigure/tags.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace prefigure {

void scatter(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    // Check for data source vs direct points
    auto data_attr = element.attribute("data");
    if (data_attr) {
        // Data-driven scatter: retrieve data set
        auto data_val = diagram.expr_ctx().retrieve(data_attr.value());

        auto x_attr = element.attribute("x");
        if (!x_attr) {
            spdlog::error("A <scatter> defined from a data source needs an @x attribute");
            return;
        }
        auto y_attr = element.attribute("y");
        if (!y_attr) {
            spdlog::error("A <scatter> defined from a data source needs a @y attribute");
            return;
        }

        // For data-driven mode, we expect the data to be accessible via math_utilities
        // Since the C++ port doesn't have full data frame support yet, we try evaluating
        // x and y fields as vectors
        // TODO: full data frame support with filter
        spdlog::warn("Data-driven scatter not fully supported yet, trying direct evaluation");
    }

    // Try direct points
    auto pts_attr = element.attribute("points");
    if (!pts_attr && !data_attr) {
        spdlog::error("A <scatter> needs a @data or @points attribute");
        return;
    }

    if (pts_attr) {
        // Evaluate points and enter into namespace
        auto pts_val = diagram.expr_ctx().eval(pts_attr.value());
        diagram.expr_ctx().enter_namespace("__scatter_points", pts_val);
    }

    // Clone the element to create a point template
    pugi::xml_document point_doc;
    auto point_element = point_doc.append_copy(element);
    point_element.set_name("point");
    point_element.attribute("p") ?
        point_element.attribute("p").set_value("point") :
        point_element.append_attribute("p").set_value("point");

    auto handle_attr = element.attribute("at");
    if (handle_attr) {
        std::string handle = std::string(handle_attr.value()) + "-point";
        point_element.attribute("at") ?
            point_element.attribute("at").set_value(handle.c_str()) :
            point_element.append_attribute("at").set_value(handle.c_str());
    }

    auto point_text_attr = element.attribute("point-text");
    if (point_text_attr) {
        point_element.attribute("annotate") ?
            point_element.attribute("annotate").set_value("yes") :
            point_element.append_attribute("annotate").set_value("yes");
        point_element.attribute("text") ?
            point_element.attribute("text").set_value(point_text_attr.value()) :
            point_element.append_attribute("text").set_value(point_text_attr.value());
    }

    // Convert this element to a repeat
    // Remove all children first
    while (element.first_child()) {
        element.remove_child(element.first_child());
    }

    element.set_name("repeat");
    element.attribute("parameter") ?
        element.attribute("parameter").set_value("point in __scatter_points") :
        element.append_attribute("parameter").set_value("point in __scatter_points");

    // Add point template as child
    element.append_copy(point_element);

    tags::parse_element(element, diagram, parent, status);
}

void histogram(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    auto data_attr = element.attribute("data");
    if (!data_attr) {
        spdlog::error("A <histogram> needs a @data attribute");
        return;
    }

    auto data_val = diagram.expr_ctx().eval(data_attr.value());
    auto& data_vec = data_val.as_vector();

    double minimum = diagram.expr_ctx().eval(get_attr(element, "min", "0")).to_double();
    double maximum;
    auto max_attr = element.attribute("max");
    if (max_attr) {
        maximum = diagram.expr_ctx().eval(max_attr.value()).to_double();
    } else {
        maximum = data_vec[0];
        for (int i = 1; i < data_vec.size(); ++i) {
            if (data_vec[i] > maximum) maximum = data_vec[i];
        }
    }

    int bins = static_cast<int>(diagram.expr_ctx().eval(
        get_attr(element, "bins", "20")).to_double());

    // Manual histogram computation (replaces scipy.ndimage.histogram)
    std::vector<double> hist(bins, 0.0);
    double delta_x = (maximum - minimum) / bins;
    for (int i = 0; i < data_vec.size(); ++i) {
        double val = data_vec[i];
        if (val < minimum || val > maximum) continue;
        int bin = static_cast<int>(std::floor((val - minimum) / delta_x));
        if (bin >= bins) bin = bins - 1;
        if (bin < 0) bin = 0;
        hist[bin]++;
    }

    // Create x_values (bin edges)
    Eigen::VectorXd x_values(bins + 1);
    for (int i = 0; i <= bins; ++i) {
        x_values[i] = minimum + (maximum - minimum) * i / bins;
    }

    // Enter into namespace
    Eigen::VectorXd hist_vec(bins);
    for (int i = 0; i < bins; ++i) hist_vec[i] = hist[i];

    diagram.expr_ctx().enter_namespace("__histogram_x", Value(x_values));
    diagram.expr_ctx().enter_namespace("__histogram_y", Value(hist_vec));
    diagram.expr_ctx().enter_namespace("__delta_x", Value(delta_x));

    // Clone element to create a rectangle template
    pugi::xml_document bin_doc;
    auto bin_element = bin_doc.append_copy(element);
    bin_element.set_name("rectangle");
    bin_element.attribute("lower-left") ?
        bin_element.attribute("lower-left").set_value("(__histogram_x[bin_num],0)") :
        bin_element.append_attribute("lower-left").set_value("(__histogram_x[bin_num],0)");
    bin_element.attribute("dimensions") ?
        bin_element.attribute("dimensions").set_value("(__delta_x,__histogram_y[bin_num])") :
        bin_element.append_attribute("dimensions").set_value("(__delta_x,__histogram_y[bin_num])");

    auto handle_attr = element.attribute("at");
    if (handle_attr) {
        std::string handle = std::string(handle_attr.value()) + "-bin";
        bin_element.attribute("at") ?
            bin_element.attribute("at").set_value(handle.c_str()) :
            bin_element.append_attribute("at").set_value(handle.c_str());
    }

    auto bin_text_attr = element.attribute("bin-text");
    if (bin_text_attr) {
        bin_element.attribute("annotate") ?
            bin_element.attribute("annotate").set_value("yes") :
            bin_element.append_attribute("annotate").set_value("yes");
        bin_element.attribute("text") ?
            bin_element.attribute("text").set_value(bin_text_attr.value()) :
            bin_element.append_attribute("text").set_value(bin_text_attr.value());
    }

    // Convert to repeat
    while (element.first_child()) {
        element.remove_child(element.first_child());
    }

    element.set_name("repeat");
    std::string param_str = "bin_num=0.." + std::to_string(bins - 1);
    element.attribute("parameter") ?
        element.attribute("parameter").set_value(param_str.c_str()) :
        element.append_attribute("parameter").set_value(param_str.c_str());

    element.append_copy(bin_element);

    tags::parse_element(element, diagram, parent, status);
}

}  // namespace prefigure
