#include "prefigure/riemann_sum.hpp"
#include "prefigure/annotations.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/group.hpp"
#include "prefigure/label.hpp"
#include "prefigure/utilities.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <set>
#include <string>
#include <vector>

namespace prefigure {

void riemann_sum(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    diagram.add_id(element, get_attr(element, "id", ""));
    std::string element_id = element.attribute("id") ? element.attribute("id").value() : "";

    // Parse partition and samples if given
    std::vector<double> partition;
    std::vector<double> samples;
    bool has_partition = false;
    bool has_samples = false;
    int N = 0;

    if (element.attribute("partition")) {
        auto pv = diagram.expr_ctx().eval(element.attribute("partition").value());
        auto& pvec = pv.as_vector();
        partition.resize(pvec.size());
        for (int i = 0; i < pvec.size(); ++i) partition[i] = pvec[i];
        N = static_cast<int>(partition.size()) - 1;
        has_partition = true;
    }
    if (element.attribute("samples")) {
        auto sv = diagram.expr_ctx().eval(element.attribute("samples").value());
        auto& svec = sv.as_vector();
        samples.resize(svec.size());
        for (int i = 0; i < svec.size(); ++i) samples[i] = svec[i];
        has_samples = true;
    }

    // If no partition, create one from N and domain
    if (!has_partition) {
        BBox bbox = diagram.bbox();
        std::array<double, 2> domain;
        auto domain_attr = element.attribute("domain");
        if (!domain_attr) {
            domain = {bbox[0], bbox[2]};
        } else {
            auto dv = diagram.expr_ctx().eval(domain_attr.value());
            auto& v = dv.as_vector();
            domain[0] = v[0];
            domain[1] = v[1];
        }

        auto n_attr = element.attribute("N");
        if (!n_attr) {
            spdlog::error("Error in <riemann-sum> setting N={}", get_attr(element, "N", ""));
            return;
        }
        N = static_cast<int>(diagram.expr_ctx().eval(n_attr.value()).to_double());

        partition.resize(N + 1);
        for (int i = 0; i <= N; ++i) {
            partition[i] = domain[0] + (domain[1] - domain[0]) * i / N;
        }
    }

    // Determine rule
    static const std::set<std::string> constant_rules = {
        "left", "right", "midpoint", "user-defined", "upper", "lower"
    };

    std::string rule = get_attr(element, "rule", "");
    if (rule.empty()) {
        if (has_samples) {
            rule = "user-defined";
        } else {
            rule = "left";
        }
    }

    // Compute samples for constant rules
    if (rule == "left") {
        samples.resize(N);
        for (int i = 0; i < N; ++i) samples[i] = partition[i];
    } else if (rule == "right") {
        samples.resize(N);
        for (int i = 0; i < N; ++i) samples[i] = partition[i + 1];
    } else if (rule == "midpoint") {
        samples.resize(N);
        for (int i = 0; i < N; ++i) samples[i] = (partition[i] + partition[i + 1]) / 2.0;
    }

    // Get the function
    MathFunction f;
    try {
        f = diagram.expr_ctx().eval(element.attribute("function").value()).as_function();
    } catch (...) {
        spdlog::error("Error in <riemann-sum> retrieving function={}",
                      get_attr(element, "function", ""));
        return;
    }

    // Handle annotations
    XmlNode annotation;
    std::string interval_text;
    bool has_annotation = false;

    if (get_attr(element, "annotate", "no") == "yes" &&
        status != OutlineStatus::AddOutline) {
        auto scratch = diagram.get_scratch();
        annotation = scratch.append_child("annotation");
        has_annotation = true;

        // Copy relevant attributes
        for (const char* attr_name : {"id", "text", "circular", "sonify", "speech"}) {
            auto a = element.attribute(attr_name);
            if (a) {
                annotation.append_attribute(attr_name).set_value(a.value());
            }
        }

        // Set ref from id
        auto ann_id = annotation.attribute("id");
        if (ann_id) {
            annotation.append_attribute("ref").set_value(ann_id.value());
        }

        // Evaluate text and speech via evaluate_text
        auto text_a = annotation.attribute("text");
        if (text_a) {
            text_a.set_value(evaluate_text(text_a.value(), diagram.expr_ctx()).c_str());
        }
        auto speech_a = annotation.attribute("speech");
        if (speech_a) {
            speech_a.set_value(evaluate_text(speech_a.value(), diagram.expr_ctx()).c_str());
        }

        diagram.push_to_annotation_branch(annotation);

        // Check for subinterval text
        auto sub_text_attr = element.attribute("subinterval-text");
        if (sub_text_attr) {
            interval_text = sub_text_attr.value();
        }
    }

    // Transform element into a group with area-under-curve sub-elements
    element.set_name("group");

    auto outline_attr = element.attribute("outline");
    if (!outline_attr) {
        element.append_attribute("outline").set_value("tactile");
    } else {
        if (std::string(outline_attr.value()) == "yes") {
            outline_attr.set_value("always");
        }
    }

    std::string stroke = get_attr(element, "stroke", "black");
    std::string fill = get_attr(element, "fill", "none");
    std::string thickness = get_attr(element, "thickness", "2");
    std::string miterlimit = get_attr(element, "miterlimit", "");

    if (diagram.output_format() == OutputFormat::Tactile) {
        if (fill != "none") fill = "lightgray";
    }

    for (int interval_num = 0; interval_num < N; ++interval_num) {
        double left = partition[interval_num];
        double right = partition[interval_num + 1];

        // Enter interval variables into namespace
        diagram.expr_ctx().enter_namespace("_interval", Value(static_cast<double>(interval_num)));

        // Format left/right with %g-style formatting
        std::string left_str = std::format("{:g}", left);
        std::string right_str = std::format("{:g}", right);
        diagram.expr_ctx().enter_namespace("_left", Value(left_str));
        diagram.expr_ctx().enter_namespace("_right", Value(right_str));

        // Create area-under-curve sub-element
        auto area = element.append_child("area-under-curve");
        std::string area_id = element_id + "_" + std::to_string(interval_num);
        area.append_attribute("id").set_value(area_id.c_str());
        std::string domain_str = "(" + left_str + "," + right_str + ")";
        area.append_attribute("domain").set_value(domain_str.c_str());
        area.append_attribute("stroke").set_value(stroke.c_str());
        area.append_attribute("fill").set_value(fill.c_str());
        area.append_attribute("thickness").set_value(thickness.c_str());
        if (!miterlimit.empty()) {
            area.append_attribute("miterlimit").set_value(miterlimit.c_str());
        }

        if (constant_rules.count(rule)) {
            double y_value = 0.0;
            if (rule == "left" || rule == "right" || rule == "midpoint" || rule == "user-defined") {
                y_value = f(Value(samples[interval_num])).to_double();
            } else if (rule == "upper") {
                int x_count = 101;
                y_value = -1e300;
                for (int j = 0; j < x_count; ++j) {
                    double x = left + (right - left) * j / (x_count - 1);
                    double y = f(Value(x)).to_double();
                    if (y > y_value) y_value = y;
                }
            } else if (rule == "lower") {
                int x_count = 101;
                y_value = 1e300;
                for (int j = 0; j < x_count; ++j) {
                    double x = left + (right - left) * j / (x_count - 1);
                    double y = f(Value(x)).to_double();
                    if (y < y_value) y_value = y;
                }
            }

            diagram.expr_ctx().enter_namespace("_height",
                Value(std::format("{:g}", y_value)));

            // Create a constant function for this interval
            std::string function_name = "__constant_" + std::to_string(interval_num);
            double captured_y = y_value;
            MathFunction constant = [captured_y](Value) -> Value {
                return Value(captured_y);
            };
            diagram.expr_ctx().enter_function(function_name, constant);
            area.append_attribute("function").set_value(function_name.c_str());
            area.append_attribute("N").set_value("1");
        } else {
            if (rule == "trapezoidal") {
                area.append_attribute("function").set_value(
                    element.attribute("function").value());
                area.append_attribute("N").set_value("1");
            } else if (rule == "simpsons") {
                double h = (right - left) / 2.0;
                double mid = left + h;
                double y0 = f(Value(left)).to_double();
                double y1 = f(Value(mid)).to_double();
                double y2 = f(Value(right)).to_double();
                double c = y1;
                double a = (y0 + y2 - 2.0 * y1) / (2.0 * h * h);
                double b = (y2 - y0) / (2.0 * h);

                std::string func_name = "__parabola_" + std::to_string(interval_num);
                double ca = a, cb = b, cc = c, cmid = mid;
                MathFunction parabola = [ca, cb, cc, cmid](Value v) -> Value {
                    double x = v.to_double();
                    return Value(ca * (x - cmid) * (x - cmid) + cb * (x - cmid) + cc);
                };
                diagram.expr_ctx().enter_function(func_name, parabola);
                area.append_attribute("function").set_value(func_name.c_str());
                area.append_attribute("N").set_value("100");
            }
        }

        // Add interval annotation if subinterval-text was specified
        if (has_annotation && !interval_text.empty()) {
            auto interval_annotation = annotation.append_child("annotation");
            interval_annotation.append_attribute("ref").set_value(area.attribute("id").value());
            interval_annotation.append_attribute("text").set_value(
                evaluate_text(interval_text, diagram.expr_ctx()).c_str());
        }
    }

    group(element, diagram, parent, status);

    // Pop from annotation branch after processing all intervals
    if (has_annotation) {
        diagram.pop_from_annotation_branch();
    }
}

}  // namespace prefigure
