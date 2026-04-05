#include "prefigure/label.hpp"
#include "prefigure/diagram.hpp"
#include "prefigure/ctm.hpp"
#include "prefigure/utilities.hpp"
#include "prefigure/user_namespace.hpp"
#include "prefigure/label_tools.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>
#include <functional>
#include <memory>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace prefigure {

// ---------------------------------------------------------------------------
// Module-level singletons
// ---------------------------------------------------------------------------

static std::unique_ptr<AbstractMathLabels> g_math_labels;
static std::unique_ptr<AbstractTextMeasurements> g_text_measurements;
static std::unique_ptr<AbstractBrailleTranslator> g_braille_translator;

AbstractMathLabels* get_math_labels() { return g_math_labels.get(); }
AbstractTextMeasurements* get_text_measurements() { return g_text_measurements.get(); }
AbstractBrailleTranslator* get_braille_translator() { return g_braille_translator.get(); }

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

static size_t utf8_char_count(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = s[i];
        if (c < 0x80) { i += 1; }
        else if (c < 0xE0) { i += 2; }
        else if (c < 0xF0) { i += 3; }
        else { i += 4; }
        ++count;
    }
    return count;
}

static const std::string nemeth_on  = "\xe2\xa0\xb8\xe2\xa0\xa9 ";  // braille chars
static const std::string nemeth_off = "\xe2\xa0\xb8\xe2\xa0\xb1 ";
static const std::string grade1_indicator = "\xe2\xa0\xb0";

static const std::set<std::string> label_tags = {"it", "b", "newline"};
static const std::set<std::string> allowed_fonts = {"serif", "sans-serif", "monospace"};

static const std::unordered_map<std::string, std::array<double, 2>> s_alignment_displacement = {
    {"southeast", {0, 0}},
    {"east",      {0, 0.5}},
    {"northeast", {0, 1}},
    {"north",     {-0.5, 1}},
    {"northwest", {-1, 1}},
    {"west",      {-1, 0.5}},
    {"southwest", {-1, 0}},
    {"south",     {-0.5, 0}},
    {"center",    {-0.5, 0.5}},
    {"se",        {0, 0}},
    {"e",         {0, 0.5}},
    {"ne",        {0, 1}},
    {"n",         {-0.5, 1}},
    {"nw",        {-1, 1}},
    {"w",         {-1, 0.5}},
    {"sw",        {-1, 0}},
    {"s",         {-0.5, 0}},
    {"c",         {-0.5, 0.5}},
    {"xaxis-label", {-0.5, 0}},
    {"ha",        {-0.5, 0}},
    {"va",        {-1, 0.5}},
    {"xl",        {-1, 1}},
};

static const std::unordered_map<std::string, std::array<double, 2>> s_braille_displacement = {
    {"southeast", {0, -1}},
    {"east",      {0, -0.5}},
    {"northeast", {0, 0}},
    {"north",     {-0.5, 0}},
    {"northwest", {-1, 0}},
    {"west",      {-1, -0.5}},
    {"southwest", {-1, -1}},
    {"south",     {-0.5, -1}},
    {"center",    {-0.5, -0.5}},
    {"se",        {0, -1}},
    {"e",         {0, -0.5}},
    {"ne",        {0, 0}},
    {"n",         {-0.5, 0}},
    {"nw",        {-1, 0}},
    {"w",         {-1, -0.5}},
    {"sw",        {-1, -1}},
    {"s",         {-0.5, -1}},
    {"c",         {-0.5, -0.5}},
    {"xaxis-label", {0, -1}},
    {"ha",        {0, -1}},
    {"hat",       {0, 0}},
    {"va",        {-1, -0.5}},
    {"var",       {0, -0.5}},
    {"xl",        {-1, 0}},
};

static const std::vector<std::string> alignment_circle = {
    "east", "northeast", "north", "northwest",
    "west", "southwest", "south", "southeast"
};

const std::unordered_map<std::string, std::array<double, 2>>& alignment_displacement_map() {
    return s_alignment_displacement;
}

const std::unordered_map<std::string, std::array<double, 2>>& braille_displacement_map() {
    return s_braille_displacement;
}

// ---------------------------------------------------------------------------
// init / add_macros
// ---------------------------------------------------------------------------

void init(OutputFormat format, Environment environment) {
    // We only support local (desktop) backends in C++.
    // Pyodide is not applicable for the C++ port.
    g_text_measurements = std::make_unique<CairoTextMeasurements>();
    g_braille_translator = std::make_unique<LocalLouisBrailleTranslator>();
    g_math_labels = std::make_unique<LocalMathLabels>(format);
}

void add_macros(const std::string& macros) {
    if (g_math_labels) {
        g_math_labels->add_macros(macros);
    }
}

// ---------------------------------------------------------------------------
// Utility functions
// ---------------------------------------------------------------------------

bool is_label_tag(const std::string& tag) {
    return label_tags.count(tag) > 0;
}

std::string evaluate_text(const std::string& text, ExpressionContext& ctx) {
    // Replace ${...} patterns with evaluated values
    static const std::regex token_re(R"(\$\{([^}]*)\})");
    std::string result;
    std::sregex_iterator it(text.begin(), text.end(), token_re);
    std::sregex_iterator end;
    size_t last_pos = 0;

    while (it != end) {
        const auto& match = *it;
        result.append(text, last_pos, match.position() - last_pos);

        std::string expr = match[1].str();
        try {
            auto val = ctx.eval(expr);
            if (val.is_double()) {
                result += std::to_string(val.as_double());
            } else if (val.is_string()) {
                result += val.as_string();
            } else if (val.is_vector()) {
                result += pt2str(val.as_vector(), ",", true);
            } else {
                result += expr;
            }
        } catch (...) {
            result += expr;
        }

        last_pos = match.position() + match.length();
        ++it;
    }
    result.append(text, last_pos, text.size() - last_pos);
    return result;
}

std::string get_alignment_from_direction(const Point2d& direction) {
    double angle = std::atan2(direction[1], direction[0]) * 180.0 / M_PI;
    int align = static_cast<int>(std::round(angle / 45.0)) % 8;
    if (align < 0) align += 8;
    return alignment_circle[align];
}

double snap_to_embossing_grid(double x) {
    return 3.6 * std::round(x / 3.6);
}

// ---------------------------------------------------------------------------
// Font face helper (color substitution)
// ---------------------------------------------------------------------------

struct FontFace {
    std::string family;
    double size;
    bool italic;
    bool bold;
    std::string color;  // empty string = no color override
};

static FontFace font_face_sub_color(const FontFace& ff, const std::string& color) {
    if (color.empty()) return ff;
    FontFace result = ff;
    result.color = color;
    return result;
}

// ---------------------------------------------------------------------------
// Text element info: [node, width, above, below]
// ---------------------------------------------------------------------------

struct TextElementInfo {
    XmlNode node;
    double width = 0;
    double above = 0;
    double below = 0;
    bool valid = false;
};

static TextElementInfo mk_text_element(const std::string& text_str, const FontFace& font_data,
                                        XmlNode label_group) {
    auto text_el = label_group.append_child("text");
    text_el.text().set(text_str.c_str());
    text_el.append_attribute("font-family").set_value(font_data.family.c_str());
    text_el.append_attribute("font-size").set_value(std::to_string(font_data.size).c_str());
    if (font_data.italic) {
        text_el.append_attribute("font-style").set_value("italic");
    }
    if (font_data.bold) {
        text_el.append_attribute("font-weight").set_value("bold");
    }
    if (!font_data.color.empty()) {
        text_el.append_attribute("fill").set_value(font_data.color.c_str());
    }

    if (!g_text_measurements) {
        return {text_el, 0, 0, 0, false};
    }

    auto measurements = g_text_measurements->measure_text(
        text_str, font_data.family, font_data.size, font_data.italic, font_data.bold);

    if (!measurements.has_value()) {
        // Remove the element we just added since we can't measure it
        label_group.remove_child(text_el);
        return {{}, 0, 0, 0, false};
    }

    auto& m = *measurements;
    return {text_el, m[0], m[1], m[2], true};
}

static TextElementInfo mk_m_element(XmlNode m_tag, Diagram& diagram, XmlNode label_group) {
    std::string m_tag_id = m_tag.attribute("id").as_string();
    auto insert = g_math_labels->get_math_label(m_tag_id);
    if (!insert) {
        return {{}, 0, 0, 0, false};
    }

    // Modify IDs on glyphs by prepending the diagram's id prefix
    auto defs = insert.child("defs");
    std::unordered_map<std::string, std::string> defs_dict;
    if (defs) {
        for (auto glyph = defs.first_child(); glyph; glyph = glyph.next_sibling()) {
            std::string id = glyph.attribute("id").as_string();
            std::string new_id = diagram.prepend_id_prefix(id);
            glyph.attribute("id").set_value(new_id.c_str());
            defs_dict[id] = new_id;
        }
    }

    // Update xlink:href references
    // Find all <use> elements recursively
    auto update_uses = [&](auto& self, XmlNode node) -> void {
        for (auto child = node.first_child(); child; child = child.next_sibling()) {
            if (std::string(child.name()) == "use") {
                auto href_attr = child.attribute("xlink:href");
                if (!href_attr) {
                    href_attr = child.attribute("href");
                }
                if (href_attr) {
                    std::string href = href_attr.as_string();
                    if (!href.empty() && href[0] == '#') {
                        std::string key = href.substr(1);
                        auto it = defs_dict.find(key);
                        if (it != defs_dict.end()) {
                            std::string new_href = "#" + it->second;
                            href_attr.set_value(new_href.c_str());
                        }
                    }
                }
            }
            self(self, child);
        }
    };
    update_uses(update_uses, insert);

    // Express dimensions in px
    // The Python code parses style, width, height attributes looking for "ex" units
    std::unordered_map<std::string, double> dim_dict;
    for (const char* attr_name : {"style", "width", "height"}) {
        std::string attr_val = insert.attribute(attr_name).as_string();
        // Find the last token and extract the number before "ex"
        std::string last_token;
        std::istringstream iss(attr_val);
        std::string token;
        while (iss >> token) {
            last_token = token;
        }
        double dimension = 0;
        auto ex_pos = last_token.find("ex");
        if (ex_pos != std::string::npos) {
            try {
                dimension = std::stod(last_token.substr(0, ex_pos)) * 8.0;
            } catch (...) {
                dimension = 0;
            }
        }
        dim_dict[attr_name] = dimension;

        // Replace with px units
        std::string pts = std::format("{:.3f}px", dimension);
        // Replace last token in the original attribute value
        auto last_space = attr_val.rfind(' ');
        std::string new_val;
        if (last_space != std::string::npos) {
            new_val = attr_val.substr(0, last_space + 1) + pts;
        } else {
            new_val = pts;
        }
        insert.attribute(attr_name).set_value(new_val.c_str());
    }

    // Copy the SVG into the label group
    auto copied = label_group.append_copy(insert);

    double width = dim_dict["width"];
    double above = dim_dict["height"] + dim_dict["style"];
    double below = -dim_dict["style"];

    // Apply color if specified
    std::string color = m_tag.attribute("color").as_string();
    if (!color.empty()) {
        std::string style = copied.attribute("style").as_string();
        if (!style.empty() && style.back() == ';') {
            style += " color:" + color;
        } else if (!style.empty()) {
            style += "; color:" + color;
        } else {
            style = "color:" + color;
        }
        if (copied.attribute("style")) {
            copied.attribute("style").set_value(style.c_str());
        } else {
            copied.append_attribute("style").set_value(style.c_str());
        }
    }

    return {copied, width, above, below, true};
}

// ---------------------------------------------------------------------------
// label_element
// ---------------------------------------------------------------------------

void label_element(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    if (status == OutlineStatus::AddOutline) {
        return;  // Not ready for labels during outline pass
    }

    // Create a group to hold the label
    auto group = parent.append_child("g");
    diagram.add_label(element, group);
    diagram.add_id(element, element.attribute("id").as_string());

    std::string elem_id = element.attribute("id").as_string();
    group.append_attribute("id").set_value(elem_id.c_str());
    diagram.register_svg_element(element, group);

    // For tactile output, don't add to parent yet
    if (diagram.output_format() == OutputFormat::Tactile) {
        parent.remove_child(group);
        // We still need the group in the scratch area
        group = diagram.get_scratch().append_child("g");
        group.append_attribute("id").set_value(elem_id.c_str());
        diagram.add_label(element, group);
    }

    // Evaluate ${...} substitutions in all text content (walk all descendants)
    std::function<void(pugi::xml_node)> walk_descendants = [&](pugi::xml_node node) {
        for (auto child : node.children()) {
            if (child.type() == pugi::node_pcdata && child.value()) {
                std::string text = child.value();
                child.set_value(evaluate_text(text, diagram.expr_ctx()).c_str());
            }
            if (child.type() == pugi::node_element) {
                walk_descendants(child);
            }
        }
    };
    walk_descendants(element);

    // Register <m> tags with MathJax
    for (auto math = element.child("m"); math; math = math.next_sibling("m")) {
        diagram.add_id(math);
        std::string math_id = math.attribute("id").as_string();
        std::string math_text = math.text().as_string();
        if (g_math_labels) {
            g_math_labels->register_math_label(math_id, math_text);
        }
    }

    // Set alignment
    std::string align = get_attr(element, "alignment", "c");
    if (align.size() > 0 && (align[0] == '2' || align == "e")) {
        align = "east";
    }
    element.attribute("alignment").set_value(align.c_str());
    if (!element.attribute("alignment")) {
        element.append_attribute("alignment").set_value(align.c_str());
    }

    // Set anchor point
    auto anchor_attr = element.attribute("anchor");
    if (anchor_attr) {
        if (!element.attribute("p")) {
            element.append_attribute("p").set_value(anchor_attr.as_string());
        } else {
            element.attribute("p").set_value(anchor_attr.as_string());
        }
    }
    std::string p_val = get_attr(element, "p", "[0,0]");
    if (!element.attribute("p")) {
        element.append_attribute("p").set_value(p_val.c_str());
    }
}

// ---------------------------------------------------------------------------
// position_svg_label
// ---------------------------------------------------------------------------

static void position_svg_label(XmlNode element, Diagram& diagram, const CTM& ctm, XmlNode group) {
    auto label_group = group.append_child("g");

    // Determine the anchor point
    Point2d p;
    try {
        std::string user_coords = get_attr(element, "user-coords", "yes");
        std::string p_str = element.attribute("p").as_string();
        Value p_val = diagram.expr_ctx().eval(p_str);
        if (user_coords == "yes") {
            p = ctm.transform(p_val.as_point());
        } else {
            p = p_val.as_point();
        }
    } catch (const std::exception& e) {
        spdlog::error("Error in label parsing anchor={}", element.attribute("p").as_string());
        return;
    }

    std::string alignment = get_attr(element, "alignment", "center");
    auto disp_it = s_alignment_displacement.find(alignment);
    if (disp_it == s_alignment_displacement.end()) {
        spdlog::error("Unknown alignment in label: {}", alignment);
        return;
    }
    auto displacement = disp_it->second;

    // Compute offset
    std::array<double, 2> offset;
    std::string abs_offset_str = get_attr(element, "abs-offset", "none");
    if (abs_offset_str == "none") {
        offset = {8.0 * (displacement[0] + 0.5), 8.0 * (displacement[1] - 0.5)};
    } else {
        try {
            Value ov = diagram.expr_ctx().eval(abs_offset_str);
            auto pt = ov.as_point();
            offset = {pt[0], pt[1]};
        } catch (...) {
            spdlog::error("Error in label parsing abs-offset={}", abs_offset_str);
            return;
        }
    }

    auto offset_attr = element.attribute("offset");
    if (offset_attr) {
        try {
            Value ov = diagram.expr_ctx().eval(offset_attr.as_string());
            auto pt = ov.as_point();
            offset[0] += pt[0];
            offset[1] += pt[1];
        } catch (...) {
            spdlog::error("Error in label parsing offset={}", offset_attr.as_string());
            return;
        }
    }

    // Extract label color
    std::string label_color = element.attribute("color").as_string();

    diagram.apply_defaults("label", element);

    std::string font_family = get_attr(element, "font", "sans-serif");
    // Lowercase and trim
    std::transform(font_family.begin(), font_family.end(), font_family.begin(), ::tolower);
    while (!font_family.empty() && font_family.front() == ' ') font_family.erase(font_family.begin());
    while (!font_family.empty() && font_family.back() == ' ') font_family.pop_back();
    if (allowed_fonts.find(font_family) == allowed_fonts.end()) {
        font_family = "sans-serif";
    }

    double font_size = 14.0;
    try {
        std::string fs_str = get_attr(element, "font-size", "14");
        font_size = diagram.expr_ctx().eval(fs_str).to_double();
    } catch (...) {}

    FontFace std_ff  = {font_family, font_size, false, false, label_color};
    FontFace it_ff   = {font_family, font_size, true,  false, label_color};
    FontFace b_ff    = {font_family, font_size, false, true,  label_color};
    FontFace it_b_ff = {font_family, font_size, true,  true,  label_color};

    // Extract text elements with their font info
    using TextPiece = std::variant<
        std::pair<std::string, FontFace>,  // text + font
        XmlNode                            // <m> element
    >;
    using Row = std::vector<TextPiece>;
    std::vector<Row> text_elements;

    // Start with the element's text content
    Row current_row;
    std::string root_text = element.text().as_string();
    current_row.push_back(std::pair<std::string, FontFace>{root_text, std_ff});

    for (auto el = element.first_child(); el; el = el.next_sibling()) {
        std::string tag = el.name();

        if (tag == "newline") {
            text_elements.push_back(std::move(current_row));
            current_row = Row{};
        }
        if (tag == "m") {
            current_row.push_back(el);
            std::string m_color = el.attribute("color").as_string();
            if (m_color.empty()) m_color = label_color;
            if (!m_color.empty()) {
                if (!el.attribute("color")) {
                    el.append_attribute("color").set_value(m_color.c_str());
                } else {
                    el.attribute("color").set_value(m_color.c_str());
                }
            }
        }
        if (tag == "plain") {
            std::string p_color = el.attribute("color").as_string();
            current_row.push_back(std::pair<std::string, FontFace>{
                el.text().as_string(), font_face_sub_color(std_ff, p_color)});
        }
        if (tag == "it") {
            std::string it_color = el.attribute("color").as_string();
            current_row.push_back(std::pair<std::string, FontFace>{
                el.text().as_string(), font_face_sub_color(it_ff, it_color)});

            for (auto child = el.first_child(); child; child = child.next_sibling()) {
                if (std::string(child.name()) != "b") {
                    spdlog::error("<{}> is not allowed inside a <it>", child.name());
                    continue;
                }
                std::string b_color = child.attribute("color").as_string();
                if (b_color.empty()) b_color = it_color;
                current_row.push_back(std::pair<std::string, FontFace>{
                    child.text().as_string(), font_face_sub_color(it_b_ff, b_color)});
                // Tail text after the <b> child
                auto next = child.next_sibling();
                if (next && next.type() == pugi::node_pcdata) {
                    current_row.push_back(std::pair<std::string, FontFace>{
                        next.value(), font_face_sub_color(it_ff, it_color)});
                }
            }
        }
        if (tag == "b") {
            std::string b_color = el.attribute("color").as_string();
            current_row.push_back(std::pair<std::string, FontFace>{
                el.text().as_string(), font_face_sub_color(b_ff, b_color)});

            for (auto child = el.first_child(); child; child = child.next_sibling()) {
                if (std::string(child.name()) != "it") {
                    spdlog::error("<{}> is not allowed inside a <b>", child.name());
                    continue;
                }
                std::string it_color = child.attribute("color").as_string();
                if (it_color.empty()) it_color = b_color;
                current_row.push_back(std::pair<std::string, FontFace>{
                    child.text().as_string(), font_face_sub_color(it_b_ff, it_color)});
                auto next = child.next_sibling();
                if (next && next.type() == pugi::node_pcdata) {
                    current_row.push_back(std::pair<std::string, FontFace>{
                        next.value(), font_face_sub_color(b_ff, b_color)});
                }
            }
        }

        // Tail text of the element
        // In pugixml, tail text is the next sibling pcdata node
        // We approximate by checking the next sibling
        auto next_sib = el.next_sibling();
        if (next_sib && next_sib.type() == pugi::node_pcdata) {
            current_row.push_back(std::pair<std::string, FontFace>{
                next_sib.value(), std_ff});
        }
    }
    text_elements.push_back(std::move(current_row));

    // Second pass: clean up empty text and create SVG elements
    using RowInfo = std::vector<TextElementInfo>;
    std::vector<RowInfo> processed_rows;

    for (auto& row : text_elements) {
        RowInfo new_row;
        for (auto& piece : row) {
            if (std::holds_alternative<std::pair<std::string, FontFace>>(piece)) {
                auto& [text, ff] = std::get<std::pair<std::string, FontFace>>(piece);
                // Trim whitespace
                std::string trimmed = text;
                while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
                while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
                if (!trimmed.empty()) {
                    auto info = mk_text_element(trimmed, ff, label_group);
                    if (info.valid) {
                        new_row.push_back(std::move(info));
                    }
                }
            } else {
                auto& m_node = std::get<XmlNode>(piece);
                auto info = mk_m_element(m_node, diagram, label_group);
                if (info.valid) {
                    new_row.push_back(std::move(info));
                }
            }
        }
        processed_rows.push_back(std::move(new_row));
    }

    // Compute dimensions per row
    double space = 4.45;
    double interline = 3.0;
    try {
        std::string il_str = get_attr(element, "interline", "3");
        interline = diagram.expr_ctx().eval(il_str).to_double();
    } catch (...) {}

    struct RowDims {
        double width = 0;
        double height = 0;
        double above = 0;
        double below = 0;
    };
    std::vector<RowDims> text_dimensions;

    for (size_t i = 0; i < processed_rows.size(); ++i) {
        auto& row = processed_rows[i];
        double w = 0;
        double max_above = 0;
        double max_below = 0;
        for (auto& el : row) {
            w += el.width;
            max_above = std::max(max_above, el.above);
            max_below = std::max(max_below, el.below);
        }
        double h = max_above + max_below;
        if (!row.empty()) {
            w += (row.size() - 1) * space;
        }
        double il = (i == processed_rows.size() - 1) ? 0.0 : interline;
        text_dimensions.push_back({w, h + il, max_above, max_below});
    }

    // Overall bounding box
    double total_width = 0;
    double total_height = 0;
    for (auto& d : text_dimensions) {
        total_width = std::max(total_width, d.width);
        total_height += d.height;
    }

    diagram.register_label_dims(element, {total_width, total_height});

    // Position each sub-element
    std::string justify = get_attr(element, "justify", "center");
    double y_location = 0;
    for (size_t i = 0; i < processed_rows.size(); ++i) {
        auto& row = processed_rows[i];
        double row_width = text_dimensions[i].width;
        double row_above = text_dimensions[i].above;
        double x_location = 0;
        if (justify == "center") {
            x_location = (total_width - row_width) / 2.0;
        } else if (justify == "right") {
            x_location = total_width - row_width;
        }

        for (auto& el : row) {
            if (!el.valid || !el.node) continue;
            el.node.append_attribute("x").set_value(float2str(x_location).c_str());
            x_location += el.width + space;

            std::string tag = el.node.name();
            if (tag == "text") {
                el.node.append_attribute("y").set_value(
                    float2str(y_location + row_above).c_str());
            } else {
                el.node.append_attribute("y").set_value(
                    float2str(y_location + row_above - el.above).c_str());
            }
        }
        y_location += text_dimensions[i].height;
    }

    // Build transform
    std::string tform = translatestr(p[0] + offset[0], p[1] - offset[1]);

    double sc = 1.0;
    try {
        std::string sc_str = get_attr(element, "scale", "1");
        sc = std::stod(sc_str);
    } catch (...) {}

    if (sc != 1.0) {
        tform += " " + scalestr(sc, sc);
    }

    auto rot_attr = element.attribute("rotate");
    if (rot_attr) {
        tform += " " + rotatestr(std::stod(rot_attr.as_string()));
    }

    tform += " " + translatestr(total_width * displacement[0], -total_height * displacement[1]);

    group.append_attribute("transform").set_value(tform.c_str());

    // Optional white background
    std::string clear_bg = get_attr(element, "clear-background", "no");
    if (clear_bg == "yes") {
        int bg_margin = 6;
        try {
            bg_margin = std::stoi(get_attr(element, "background-margin", "6"));
        } catch (...) {}
        auto rect = group.prepend_child("rect");
        rect.append_attribute("x").set_value(std::to_string(-bg_margin).c_str());
        rect.append_attribute("y").set_value(std::to_string(-bg_margin).c_str());
        rect.append_attribute("width").set_value(std::to_string(total_width + 2 * bg_margin).c_str());
        rect.append_attribute("height").set_value(std::to_string(total_height + 2 * bg_margin).c_str());
        rect.append_attribute("stroke").set_value("none");
        rect.append_attribute("fill").set_value("white");
    }

    // Record expression attribute for annotation lookup
    diagram.add_id(label_group, element.attribute("expr").as_string());
}

// ---------------------------------------------------------------------------
// position_braille_label
// ---------------------------------------------------------------------------

static void position_braille_label(XmlNode element, Diagram& diagram, const CTM& ctm,
                                    XmlNode background_group, XmlNode braille_group) {
    auto group = braille_group.append_child("g");
    group.append_attribute("id").set_value(element.attribute("id").as_string());

    // Determine anchor point
    Point2d p;
    try {
        std::string user_coords = get_attr(element, "user-coords", "yes");
        std::string p_str = element.attribute("p").as_string();
        Value p_val = diagram.expr_ctx().eval(p_str);
        if (user_coords == "yes") {
            p = ctm.transform(p_val.as_point());
        } else {
            p = p_val.as_point();
        }
    } catch (...) {
        spdlog::error("Error in label parsing anchor={}", element.attribute("p").as_string());
        return;
    }

    std::string alignment = get_attr(element, "alignment", "center");
    auto disp_it = s_braille_displacement.find(alignment);
    if (disp_it == s_braille_displacement.end()) {
        spdlog::error("Unknown alignment in label: {}", alignment);
        return;
    }
    auto displacement = disp_it->second;

    // Compute offset
    std::array<double, 2> offset;
    std::string abs_offset_str = get_attr(element, "abs-offset", "none");
    if (abs_offset_str == "none") {
        offset = {8.0 * (displacement[0] + 0.5), 8.0 * (displacement[1] + 0.5)};
    } else {
        try {
            Value ov = diagram.expr_ctx().eval(abs_offset_str);
            auto pt = ov.as_point();
            offset = {pt[0], pt[1]};
        } catch (...) {
            spdlog::error("Error in label parsing abs-offset");
            return;
        }
    }

    // Apply sign-based adjustments
    auto sign_fn = [](double x) -> double {
        return (x > 0) ? 1.0 : (x < 0) ? -1.0 : 0.0;
    };
    offset[0] += 6.0 * sign_fn(offset[0]);
    offset[1] += 6.0 * sign_fn(offset[1]);

    if (displacement[0] == 0) offset[0] += 6;
    if (displacement[1] == -1) offset[1] -= 6;
    if (alignment == "n" || alignment == "north") offset[1] += 5;

    double gap = 3.6;
    if (alignment == "ha") { offset = {-4 * gap, -30}; }
    if (alignment == "hat") { offset = {-4 * gap, 30}; }
    if (alignment == "va") { offset = {-9, 0}; }
    if (alignment == "var") { offset = {30, 0}; }
    if (alignment == "xl") { offset = {-10, 12}; }

    auto offset_attr = element.attribute("offset");
    if (offset_attr) {
        try {
            Value ov = diagram.expr_ctx().eval(offset_attr.as_string());
            auto pt = ov.as_point();
            offset[0] += pt[0];
            offset[1] += pt[1];
        } catch (...) {
            spdlog::error("Error in label parsing offset={}", offset_attr.as_string());
            return;
        }
    }

    p[0] += offset[0];
    p[1] -= offset[1];

    // Assemble text pieces for braille translation
    struct BraillePiece {
        bool is_math = false;
        std::string text;
        std::string style;  // "plain", "it", "b"
        std::string math_id;
    };
    using BRow = std::vector<BraillePiece>;
    std::vector<BRow> text_elements;

    BRow current_row;
    std::string root_text = element.text().as_string();
    current_row.push_back({false, root_text, "plain", ""});

    for (auto el = element.first_child(); el; el = el.next_sibling()) {
        std::string tag = el.name();
        if (tag == "newline") {
            text_elements.push_back(std::move(current_row));
            current_row = BRow{};
        }
        if (tag == "m") {
            current_row.push_back({true, el.text().as_string(), "", el.attribute("id").as_string()});
        }
        if (tag == "it") {
            current_row.push_back({false, el.text().as_string(), "it", ""});
            for (auto child = el.first_child(); child; child = child.next_sibling()) {
                if (std::string(child.name()) != "b") {
                    spdlog::error("<{}> is not allowed inside a <it>", child.name());
                    continue;
                }
                current_row.push_back({false, child.text().as_string(), "it", ""});
            }
        }
        if (tag == "b") {
            current_row.push_back({false, el.text().as_string(), "b", ""});
            for (auto child = el.first_child(); child; child = child.next_sibling()) {
                if (std::string(child.name()) != "it") {
                    spdlog::error("<{}> is not allowed inside a <b>", child.name());
                    continue;
                }
                current_row.push_back({false, child.text().as_string(), "b", ""});
            }
        }
        if (tag == "plain") {
            current_row.push_back({false, el.text().as_string(), "plain", ""});
        }
        // tail text
        auto next_sib = el.next_sibling();
        if (next_sib && next_sib.type() == pugi::node_pcdata) {
            current_row.push_back({false, next_sib.value(), "plain", ""});
        }
    }
    text_elements.push_back(std::move(current_row));

    // Clean pass: remove empty, merge same-style
    for (auto& row : text_elements) {
        BRow new_row;
        for (auto& piece : row) {
            if (piece.is_math) {
                new_row.push_back(std::move(piece));
                continue;
            }
            std::string text = piece.text;
            // Trim
            while (!text.empty() && text.front() == ' ') text.erase(text.begin());
            while (!text.empty() && text.back() == ' ') text.pop_back();
            if (text.empty()) continue;

            if (!new_row.empty() && !new_row.back().is_math &&
                new_row.back().style == piece.style) {
                new_row.back().text += " " + text;
            } else {
                piece.text = text;
                new_row.push_back(std::move(piece));
            }
        }
        row = std::move(new_row);
    }

    // Translate to braille
    std::unordered_map<std::string, int> typeform_dict = {
        {"plain", 0}, {"it", 1}, {"b", 4}
    };

    std::vector<std::string> braille_rows;
    for (auto& row : text_elements) {
        std::string row_text;
        bool needs_grade1 = false;

        for (auto& piece : row) {
            if (!piece.is_math) {
                std::string text = piece.text;
                if (!row_text.empty()) text = " " + text;
                int tf_val = typeform_dict[piece.style];
                std::vector<int> typeform(text.size(), tf_val);
                auto result = g_braille_translator->translate(text, typeform);
                if (result.has_value()) {
                    row_text += *result;
                }
            } else {
                // Math element
                std::string m_text = piece.text;
                while (!m_text.empty() && m_text.front() == ' ') m_text.erase(m_text.begin());
                while (!m_text.empty() && m_text.back() == ' ') m_text.pop_back();
                if (m_text.size() == 1) {
                    int dist = static_cast<int>(m_text[0]) - static_cast<int>('a');
                    if (dist >= 0 && dist < 26) {
                        needs_grade1 = true;
                    }
                }
                std::string braille_math = g_math_labels->get_math_braille(piece.math_id);
                if (!braille_math.empty()) {
                    if (!row_text.empty()) row_text += " ";
                    row_text += braille_math;
                }
            }
        }

        // Grade 1 indicator for single letter
        if (utf8_char_count(row_text) == 1 && needs_grade1) {
            row_text = grade1_indicator + row_text;
        }
        braille_rows.push_back(std::move(row_text));
    }

    // Compute dimensions
    double interline = 28.8;  // 0.4 inches
    size_t max_len = 0;
    for (auto& row : braille_rows) {
        max_len = std::max(max_len, utf8_char_count(row));
    }
    double width = 5.0 * gap * max_len;
    double height = 5.0 * gap + interline * (braille_rows.size() - 1);

    diagram.register_label_dims(element, {width, height});

    p[0] += width * displacement[0];
    p[1] -= height * displacement[1];

    // Snap to embossing grid
    p[0] = 3.6 * std::round(p[0] / 3.6);
    p[1] = 3.6 * std::round(p[1] / 3.6);

    std::string tform = translatestr(p[0], p[1]);
    group.append_attribute("transform").set_value(tform.c_str());

    // White background
    double bg_margin = 9.0;
    auto rect = background_group.append_child("rect");
    rect.append_attribute("id").set_value(
        (std::string(element.attribute("id").as_string()) + "-background").c_str());
    rect.append_attribute("x").set_value(float2str(p[0] - bg_margin).c_str());
    rect.append_attribute("y").set_value(float2str(p[1] - height - bg_margin).c_str());
    rect.append_attribute("width").set_value(float2str(width + 2 * bg_margin).c_str());
    rect.append_attribute("height").set_value(float2str(height + 2 * bg_margin).c_str());
    rect.append_attribute("stroke").set_value("none");
    rect.append_attribute("fill").set_value("white");

    // Place braille text elements
    std::string justify_str = get_attr(element, "justify", "center");
    double x = 0;
    double y = -height + 5.0 * gap;
    for (auto& row : braille_rows) {
        auto text_el = group.append_child("text");
        double x_line = x;
        size_t row_chars = utf8_char_count(row);
        if (justify_str == "right") {
            x_line = x + width - 5.0 * gap * row_chars;
        }
        if (justify_str == "center") {
            x_line = x + (width - 5.0 * gap * row_chars) / 2.0;
            x_line = gap * std::round(x_line / gap);
        }
        text_el.append_attribute("x").set_value(float2str(x_line).c_str());
        text_el.append_attribute("y").set_value(float2str(y).c_str());
        text_el.text().set(row.c_str());
        text_el.append_attribute("font-family").set_value("Braille29");
        text_el.append_attribute("font-size").set_value("29px");
        y += interline;
    }
}

// ---------------------------------------------------------------------------
// place_labels
// ---------------------------------------------------------------------------

void place_labels(Diagram& diagram, const std::string& filename, XmlNode root,
                  std::unordered_map<size_t, std::tuple<XmlNode, XmlNode, CTM>>& label_group_dict) {
    if (label_group_dict.empty()) {
        return;
    }

    if (g_math_labels) {
        g_math_labels->process_math_labels();
    }

    // For braille output, create container groups
    XmlNode background_group, braille_group;
    if (diagram.output_format() == OutputFormat::Tactile) {
        if (!g_braille_translator || !g_braille_translator->initialized()) {
            return;
        }
        background_group = root.append_child("g");
        background_group.append_attribute("id").set_value("background-group");
        braille_group = root.append_child("g");
        braille_group.append_attribute("id").set_value("braille-group");
    }

    for (auto& [key, elem_group_ctm] : label_group_dict) {
        auto& [element, group, ctm_val] = elem_group_ctm;

        if (diagram.output_format() == OutputFormat::Tactile) {
            position_braille_label(element, diagram, ctm_val, background_group, braille_group);
        } else {
            position_svg_label(element, diagram, ctm_val, group);
        }
    }
}

// ---------------------------------------------------------------------------
// caption
// ---------------------------------------------------------------------------

void caption(XmlNode element, Diagram& diagram, XmlNode parent, OutlineStatus status) {
    (void)parent;
    (void)status;

    if (diagram.caption_suppressed()) {
        return;
    }

    std::string text = element.text().as_string();
    if (text.empty()) {
        return;
    }

    // Trim
    while (!text.empty() && (text.front() == ' ' || text.front() == '\n')) text.erase(text.begin());
    while (!text.empty() && (text.back() == ' ' || text.back() == '\n')) text.pop_back();

    if (text.empty()) return;

    diagram.set_caption(text);
}

}  // namespace prefigure
