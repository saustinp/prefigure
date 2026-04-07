#include "prefigure/utilities.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/user_namespace.hpp"

#include <spdlog/spdlog.h>

#include <format>

namespace prefigure {

// Named color map
static const std::unordered_map<std::string, std::string> color_map = {
    {"gray", "#777"},
    {"lightgray", "#ccc"},
    {"darkgray", "#333"},
};

std::string get_color(const std::string& color) {
    if (color.empty() || color == "none") return "none";
    auto it = color_map.find(color);
    if (it != color_map.end()) return it->second;
    return color;
}

void add_attr(XmlNode element, const std::unordered_map<std::string, std::string>& attrs) {
    for (const auto& [k, v] : attrs) {
        auto existing = element.attribute(k.c_str());
        if (existing) {
            existing.set_value(v.c_str());
        } else {
            element.append_attribute(k.c_str()).set_value(v.c_str());
        }
    }
}

std::string get_attr(XmlNode element, const std::string& attr, const std::string& default_val) {
    auto attrib = element.attribute(attr.c_str());
    if (!attrib) return default_val;
    return attrib.value();
}

std::string get_attr(XmlNode element, ExpressionContext& ctx,
                     const std::string& attr, const std::string& default_val) {
    std::string raw;
    auto attrib = element.attribute(attr.c_str());
    raw = attrib ? std::string(attrib.value()) : default_val;
    if (raw.empty()) return raw;
    try {
        Value v = ctx.eval(raw);
        if (v.is_string()) return v.as_string();
        if (v.is_double()) return std::format("{}", v.as_double());
        if (v.is_vector()) {
            const auto& vec = v.as_vector();
            std::string out;
            for (Eigen::Index i = 0; i < vec.size(); ++i) {
                if (i > 0) out += ",";
                out += std::format("{:.4f}", vec[i]);
            }
            return out;
        }
        // Non-stringifiable result -- return raw
        return raw;
    } catch (...) {
        return raw;
    }
}

void set_attr(XmlNode element, const std::string& attr, const std::string& default_val) {
    std::string value = get_attr(element, attr, default_val);
    // In the full implementation, evaluate_text() would be called here
    // to substitute ${...} expressions. For now, just set the raw value.
    if (element.attribute(attr.c_str())) {
        element.attribute(attr.c_str()).set_value(value.c_str());
    } else {
        element.append_attribute(attr.c_str()).set_value(value.c_str());
    }
}

std::unordered_map<std::string, std::string> get_1d_attr(XmlNode element) {
    std::unordered_map<std::string, std::string> d;

    auto stroke = element.attribute("stroke");
    if (stroke) d["stroke"] = get_color(stroke.value());

    auto stroke_opacity = element.attribute("stroke-opacity");
    if (stroke_opacity) d["stroke-opacity"] = stroke_opacity.value();

    auto opacity = element.attribute("opacity");
    if (opacity) d["opacity"] = opacity.value();

    auto thickness = element.attribute("thickness");
    if (thickness) d["stroke-width"] = thickness.value();

    auto miterlimit = element.attribute("miterlimit");
    if (miterlimit) d["stroke-miterlimit"] = miterlimit.value();

    auto linejoin = element.attribute("linejoin");
    if (linejoin) d["stroke-linejoin"] = linejoin.value();

    auto linecap = element.attribute("linecap");
    if (linecap) d["stroke-linecap"] = linecap.value();

    auto dash = element.attribute("dash");
    if (dash) d["stroke-dasharray"] = dash.value();

    auto fill = element.attribute("fill");
    d["fill"] = fill ? fill.value() : "none";

    return d;
}

std::unordered_map<std::string, std::string> get_2d_attr(XmlNode element) {
    auto d = get_1d_attr(element);

    auto fill = element.attribute("fill");
    d["fill"] = fill ? get_color(fill.value()) : "none";

    auto fill_rule = element.attribute("fill-rule");
    if (fill_rule) d["fill-rule"] = fill_rule.value();

    auto fill_opacity = element.attribute("fill-opacity");
    if (fill_opacity) d["fill-opacity"] = fill_opacity.value();

    return d;
}

void cliptobbox(XmlNode g_element, XmlNode element, Diagram& diagram) {
    auto clip_attr = element.attribute("cliptobbox");
    if (!clip_attr || std::string(clip_attr.value()) == "no") return;

    std::string clippath_id = diagram.get_clippath();
    if (!clippath_id.empty()) {
        std::string clip_url = std::format("url(#{})", clippath_id);
        if (g_element.attribute("clip-path")) {
            g_element.attribute("clip-path").set_value(clip_url.c_str());
        } else {
            g_element.append_attribute("clip-path").set_value(clip_url.c_str());
        }
    }
}

std::string float2str(double x) {
    return std::format("{:.1f}", x);
}

std::string float2longstr(double x) {
    return std::format("{:.4f}", x);
}

std::string pt2str(const Point2d& p, const std::string& spacer, bool paren) {
    std::string text = std::format("{:.1f}", p[0]) + spacer + std::format("{:.1f}", p[1]);
    if (paren) return "(" + text + ")";
    return text;
}

std::string pt2str(const Eigen::VectorXd& p, const std::string& spacer, bool paren) {
    std::string text;
    for (Eigen::Index i = 0; i < p.size(); ++i) {
        if (i > 0) text += spacer;
        text += std::format("{:.1f}", p[i]);
    }
    if (paren) return "(" + text + ")";
    return text;
}

std::string pt2long_str(const Point2d& p, const std::string& spacer) {
    return std::format("{:.4f}", p[0]) + spacer + std::format("{:.4f}", p[1]);
}

std::string np2str(const Point2d& p) {
    return pt2str(p, ",", true);
}

}  // namespace prefigure
