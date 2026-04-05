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
        Value data_val;
        try {
            data_val = diagram.expr_ctx().retrieve(data_attr.value());
        } catch (...) {
            spdlog::error("Could not retrieve data source: {}", data_attr.value());
            return;
        }

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

        std::string x_field = x_attr.value();
        std::string y_field = y_attr.value();

        Eigen::VectorXd x_data, y_data;

        // Handle filter if present
        auto filter_attr = element.attribute("filter");
        if (filter_attr) {
            try {
                auto filter_val = diagram.expr_ctx().eval(filter_attr.value());
                if (filter_val.is_vector() && filter_val.as_vector().size() == 2) {
                    // filter evaluates to (field, value) pair
                    // For now, log a warning since full data frame filtering requires
                    // the data to be a dict-like structure
                    spdlog::warn("Data filter not fully supported in C++ port");
                }
            } catch (...) {
                spdlog::warn("Could not evaluate filter expression");
            }
        }

        // Try to get x and y data from the data object
        // If data is a matrix, treat x_field and y_field as column indices
        // If x_field/y_field are namespace variables (vectors), use them directly
        if (data_val.is_matrix()) {
            auto& mat = data_val.as_matrix();
            try {
                int x_col = std::stoi(x_field);
                int y_col = std::stoi(y_field);
                x_data = mat.col(x_col);
                y_data = mat.col(y_col);
            } catch (...) {
                spdlog::error("For matrix data, @x and @y should be column indices");
                return;
            }
        } else {
            // Try retrieving x_field and y_field as namespace variables
            try {
                x_data = diagram.expr_ctx().retrieve(x_field).as_vector();
                y_data = diagram.expr_ctx().retrieve(y_field).as_vector();
            } catch (...) {
                spdlog::error("Could not retrieve x={} or y={} from namespace", x_field, y_field);
                return;
            }
        }

        // Zip x and y into points
        auto zipped = zip_lists(x_data, y_data);
        Eigen::VectorXd points_flat(static_cast<Eigen::Index>(zipped.size() * 2));
        for (size_t i = 0; i < zipped.size(); ++i) {
            points_flat[static_cast<Eigen::Index>(2 * i)] = zipped[i][0];
            points_flat[static_cast<Eigen::Index>(2 * i + 1)] = zipped[i][1];
        }
        diagram.expr_ctx().enter_namespace("__scatter_points", Value(points_flat));
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
