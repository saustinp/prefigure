#include "prefigure/label_tools.hpp"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifdef PREFIGURE_HAS_CAIRO
#include <cairo/cairo.h>
#include <cairo/cairo-svg.h>
#endif

#ifdef PREFIGURE_HAS_LIBLOUIS
#include <liblouis/liblouis.h>
#endif

namespace prefigure {

// ===========================================================================
// LocalMathLabels
// ===========================================================================

LocalMathLabels::LocalMathLabels(OutputFormat format)
    : format_(format)
{
    auto html = html_doc_.append_child("html");
    html_body_ = html.append_child("body");
}

void LocalMathLabels::add_macros(const std::string& macros) {
    auto div = html_body_.append_child("div");
    div.append_attribute("id").set_value("latex-macros");
    std::string content = "\\(" + macros + "\\)";
    div.text().set(content.c_str());
}

void LocalMathLabels::register_math_label(const std::string& id, const std::string& text) {
    auto div = html_body_.append_child("div");
    div.append_attribute("id").set_value(id.c_str());
    std::string content = "\\(" + text + "\\)";
    div.text().set(content.c_str());
    labels_present_ = true;
}

void LocalMathLabels::process_math_labels() {
    if (!labels_present_) {
        return;
    }

    // Create temp directory
    auto tmp_dir = std::filesystem::temp_directory_path() / "prefigure-labels";
    std::filesystem::create_directories(tmp_dir);

    std::string input_filename = "prefigure-labels.html";
    std::string fmt_str = (format_ == OutputFormat::Tactile) ? "tactile" : "svg";
    std::string output_filename = "prefigure-" + fmt_str + ".html";

    auto mj_input = tmp_dir / input_filename;
    auto mj_output = tmp_dir / output_filename;

    // Write the HTML file
    if (!html_doc_.save_file(mj_input.c_str(), "  ",
                             pugi::format_indent | pugi::format_no_declaration)) {
        spdlog::error("Failed to write MathJax input file {}", mj_input.string());
        return;
    }

    // Determine options
    std::string options;
    std::string format_flag;
    if (format_ == OutputFormat::Tactile) {
        format_flag = "braille";
    } else {
        options = "--svgenhanced --depth deep";
        format_flag = "svg";
    }

    // Find mj-sre-page.js relative to this library's location.
    // We search a few standard places.
    std::string mj_script;
    std::vector<std::filesystem::path> search_paths;

    // Look relative to the executable or relative to the source tree
    // The Python version looks relative to the .py file in core/mj_sre/
    // For the C++ port we look in a few standard locations.
    auto exe_path = std::filesystem::current_path();
    search_paths.push_back(exe_path / "mj_sre");
    search_paths.push_back(exe_path / "core" / "mj_sre");

    // Also try relative to the project source
    // Walk up from cwd looking for prefig/core/mj_sre
    for (auto p = exe_path; p != p.parent_path(); p = p.parent_path()) {
        search_paths.push_back(p / "prefig" / "core" / "mj_sre");
        search_paths.push_back(p / "mj_sre");
    }

    // Also check PREFIGURE_MJ_DIR environment variable
    const char* mj_env = std::getenv("PREFIGURE_MJ_DIR");
    if (mj_env) {
        search_paths.insert(search_paths.begin(), std::filesystem::path(mj_env));
    }

    for (const auto& sp : search_paths) {
        auto candidate = sp / "mj-sre-page.js";
        if (std::filesystem::exists(candidate)) {
            mj_script = candidate.string();
            break;
        }
    }

    if (mj_script.empty()) {
        spdlog::error("Cannot find mj-sre-page.js for MathJax processing");
        spdlog::error("Set PREFIGURE_MJ_DIR environment variable to the directory containing mj-sre-page.js");
        return;
    }

    std::string mj_command = "node " + mj_script +
        " --" + format_flag + " " + options + " " +
        mj_input.string() + " > " + mj_output.string();

    spdlog::debug("Using MathJax to produce mathematical labels");
    spdlog::debug("MathJax command: {}", mj_command);

    int ret = std::system(mj_command.c_str());
    if (ret != 0) {
        spdlog::error("Production of mathematical labels with MathJax was unsuccessful (exit code {})", ret);
        return;
    }

    // Parse the output
    auto result = label_tree_.load_file(mj_output.c_str());
    if (!result) {
        spdlog::error("Failed to parse MathJax output: {}", result.description());
        return;
    }
    label_tree_valid_ = true;

    // Clean up temp files
    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
}

XmlNode LocalMathLabels::get_math_label(const std::string& id) {
    if (!label_tree_valid_) {
        spdlog::error("MathJax label tree is not available");
        return XmlNode();
    }

    if (format_ == OutputFormat::Tactile) {
        // For tactile, we don't return SVG nodes
        return XmlNode();
    }

    // XPath: //div[@id='ID']/mjx-data/mjx-container/svg
    // pugixml does not handle namespaces in XPath the same way as lxml,
    // so we navigate manually.
    std::string xpath = "//div[@id='" + id + "']";
    auto div = label_tree_.select_node(xpath.c_str()).node();
    if (!div) {
        spdlog::error("Error retrieving a mathematical label for id={}", id);
        spdlog::error("  Perhaps it was not created due to an earlier error");
        return XmlNode();
    }

    // Navigate: div -> mjx-data -> mjx-container -> svg
    auto mjx_data = div.child("mjx-data");
    if (!mjx_data) {
        spdlog::error("Error in processing label, missing mjx-data for id={}", id);
        return XmlNode();
    }

    auto mjx_container = mjx_data.child("mjx-container");
    if (!mjx_container) {
        spdlog::error("Error in processing label, missing mjx-container for id={}", id);
        return XmlNode();
    }

    auto svg = mjx_container.child("svg");
    if (!svg) {
        spdlog::error("Error in processing label, possibly a LaTeX error: {}", div.text().get());
        return XmlNode();
    }

    return svg;
}

std::string LocalMathLabels::get_math_braille(const std::string& id) {
    if (!label_tree_valid_) {
        return "";
    }

    // Check cache first
    auto it = braille_cache_.find(id);
    if (it != braille_cache_.end()) {
        return it->second;
    }

    std::string xpath = "//div[@id='" + id + "']";
    auto div = label_tree_.select_node(xpath.c_str()).node();
    if (!div) {
        spdlog::error("Error retrieving a mathematical label for id={}", id);
        return "";
    }

    auto mjx_data = div.child("mjx-data");
    if (!mjx_data) {
        spdlog::error("Error in processing label, missing mjx-data for id={}", id);
        return "";
    }

    auto mjx_braille = mjx_data.child("mjx-braille");
    if (!mjx_braille) {
        spdlog::error("Error in processing label, possibly a LaTeX error: {}", div.text().get());
        return "";
    }

    std::string text = mjx_braille.text().get();
    braille_cache_[id] = text;
    return text;
}

// ===========================================================================
// CairoTextMeasurements
// ===========================================================================

#ifdef PREFIGURE_HAS_CAIRO

struct CairoTextMeasurements::Impl {
    cairo_surface_t* surface = nullptr;
    cairo_t* context = nullptr;

    Impl() {
        surface = cairo_svg_surface_create(nullptr, 200, 200);
        context = cairo_create(surface);
    }

    ~Impl() {
        if (context) cairo_destroy(context);
        if (surface) cairo_surface_destroy(surface);
    }
};

CairoTextMeasurements::CairoTextMeasurements()
    : impl_(std::make_unique<Impl>())
{
    spdlog::info("Cairo text measurement initialized");
}

CairoTextMeasurements::~CairoTextMeasurements() = default;

std::optional<std::array<double, 3>> CairoTextMeasurements::measure_text(
    const std::string& text,
    const std::string& family,
    double size,
    bool italic,
    bool bold)
{
    if (!impl_ || !impl_->context) {
        return std::nullopt;
    }

    cairo_font_slant_t slant = italic ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL;
    cairo_font_weight_t weight = bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL;

    cairo_select_font_face(impl_->context, family.c_str(), slant, weight);
    cairo_set_font_size(impl_->context, size);

    cairo_text_extents_t extents;
    cairo_text_extents(impl_->context, text.c_str(), &extents);

    double y_bearing = extents.y_bearing;
    double t_height = extents.height;
    double x_advance = extents.x_advance;

    return std::array<double, 3>{x_advance, -y_bearing, t_height + y_bearing};
}

#endif // PREFIGURE_HAS_CAIRO

// ===========================================================================
// LocalLouisBrailleTranslator
// ===========================================================================

#ifdef PREFIGURE_HAS_LIBLOUIS

LocalLouisBrailleTranslator::LocalLouisBrailleTranslator() {
    // Test if liblouis tables are available
    // We simply set the flag; actual translation will test it
    louis_loaded_ = true;
    spdlog::info("liblouis braille translator initialized");
}

bool LocalLouisBrailleTranslator::initialized() const {
    return louis_loaded_;
}

std::optional<std::string> LocalLouisBrailleTranslator::translate(
    const std::string& text,
    const std::vector<int>& typeform)
{
    if (!louis_loaded_ || text.empty()) {
        if (text.empty()) return std::string("");
        return std::nullopt;
    }

    // Convert UTF-8 string to widechar array for liblouis
    std::vector<widechar> inbuf;
    for (size_t i = 0; i < text.size(); ) {
        uint32_t cp = 0;
        unsigned char c = text[i];
        if (c < 0x80) { cp = c; i += 1; }
        else if (c < 0xE0) { cp = (c & 0x1F) << 6 | (static_cast<unsigned char>(text[i+1]) & 0x3F); i += 2; }
        else if (c < 0xF0) { cp = (c & 0x0F) << 12 | (static_cast<unsigned char>(text[i+1]) & 0x3F) << 6 | (static_cast<unsigned char>(text[i+2]) & 0x3F); i += 3; }
        else { cp = (c & 0x07) << 18 | (static_cast<unsigned char>(text[i+1]) & 0x3F) << 12 | (static_cast<unsigned char>(text[i+2]) & 0x3F) << 6 | (static_cast<unsigned char>(text[i+3]) & 0x3F); i += 4; }
        inbuf.push_back(static_cast<widechar>(cp));
    }
    int inlen = static_cast<int>(inbuf.size());
    int outlen = inlen * 4 + 64;  // generous output buffer

    std::vector<widechar> outbuf(outlen);

    // Build typeform array (same length as input)
    std::vector<formtype> tf(inlen, 0);
    for (int i = 0; i < inlen && i < static_cast<int>(typeform.size()); ++i) {
        tf[i] = static_cast<formtype>(typeform[i]);
    }

    int in_len_copy = inlen;
    int out_len_copy = outlen;

    int result = lou_translateString(
        "en-ueb-g2.ctb",
        inbuf.data(), &in_len_copy,
        outbuf.data(), &out_len_copy,
        tf.data(),
        nullptr,  // spacing
        0         // mode
    );

    if (result == 0) {
        spdlog::error("liblouis translation failed");
        return std::nullopt;
    }

    // Convert output widechar back to UTF-8 string
    std::string output;
    output.reserve(out_len_copy * 4);
    for (int i = 0; i < out_len_copy; ++i) {
        widechar wc = outbuf[i];
        if (wc < 0x80) {
            output.push_back(static_cast<char>(wc));
        } else if (wc < 0x800) {
            output.push_back(static_cast<char>(0xC0 | (wc >> 6)));
            output.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
        } else {
            output.push_back(static_cast<char>(0xE0 | (wc >> 12)));
            output.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F)));
            output.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
        }
    }

    // Trim trailing whitespace (matching Python's .rstrip())
    while (!output.empty() && (output.back() == ' ' || output.back() == '\t' ||
                                output.back() == '\n' || output.back() == '\r')) {
        output.pop_back();
    }

    return output;
}

#endif // PREFIGURE_HAS_LIBLOUIS

}  // namespace prefigure
