#pragma once

#include "types.hpp"

#include <pugixml.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace prefigure {

// ---------------------------------------------------------------------------
// Abstract interfaces
// ---------------------------------------------------------------------------

/// Abstract base for math label processing (MathJax or browser-side).
class AbstractMathLabels {
public:
    virtual ~AbstractMathLabels() = default;

    /// Register LaTeX macro definitions.
    virtual void add_macros(const std::string& macros) = 0;

    /// Queue a math expression for batch processing.
    virtual void register_math_label(const std::string& id, const std::string& text) = 0;

    /// Run the batch processor (MathJax, etc.) over all queued labels.
    virtual void process_math_labels() = 0;

    /// Retrieve the processed label.
    /// For SVG format returns an SVG xml_node; for tactile returns a null node
    /// (use get_math_braille instead).
    virtual XmlNode get_math_label(const std::string& id) = 0;

    /// Retrieve the braille text for a tactile math label.
    virtual std::string get_math_braille(const std::string& id) = 0;
};

/// Abstract base for text measurement (width, ascent, descent).
class AbstractTextMeasurements {
public:
    virtual ~AbstractTextMeasurements() = default;

    /// Measure text.  font_data = [family, size, italic, bold].
    /// Returns {x_advance, above_baseline, below_baseline} or nullopt.
    virtual std::optional<std::array<double, 3>> measure_text(
        const std::string& text,
        const std::string& family,
        double size,
        bool italic,
        bool bold) = 0;
};

/// Abstract base for braille translation.
class AbstractBrailleTranslator {
public:
    virtual ~AbstractBrailleTranslator() = default;

    /// True if the underlying library was successfully loaded.
    virtual bool initialized() const = 0;

    /// Translate text to braille.  typeform: 0=plain, 1=italic, 4=bold.
    virtual std::optional<std::string> translate(
        const std::string& text,
        const std::vector<int>& typeform) = 0;
};

// ---------------------------------------------------------------------------
// Local (desktop) implementations
// ---------------------------------------------------------------------------

/// MathJax-based math label processor that shells out to Node.js.
class LocalMathLabels : public AbstractMathLabels {
public:
    explicit LocalMathLabels(OutputFormat format);

    void add_macros(const std::string& macros) override;
    void register_math_label(const std::string& id, const std::string& text) override;
    void process_math_labels() override;
    XmlNode get_math_label(const std::string& id) override;
    std::string get_math_braille(const std::string& id) override;

private:
    OutputFormat format_;
    bool labels_present_ = false;

    // HTML document built up for MathJax input
    pugi::xml_document html_doc_;
    pugi::xml_node html_body_;

    // Parsed MathJax output
    pugi::xml_document label_tree_;
    bool label_tree_valid_ = false;

    // Braille text cache (tactile mode)
    std::unordered_map<std::string, std::string> braille_cache_;
};

// ---------------------------------------------------------------------------
// Cairo text measurement (optional)
// ---------------------------------------------------------------------------

#ifdef PREFIGURE_HAS_CAIRO

class CairoTextMeasurements : public AbstractTextMeasurements {
public:
    CairoTextMeasurements();
    ~CairoTextMeasurements() override;

    std::optional<std::array<double, 3>> measure_text(
        const std::string& text,
        const std::string& family,
        double size,
        bool italic,
        bool bold) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

#else

/// Stub when Cairo is not available -- always returns nullopt.
class CairoTextMeasurements : public AbstractTextMeasurements {
public:
    std::optional<std::array<double, 3>> measure_text(
        const std::string& /*text*/,
        const std::string& /*family*/,
        double /*size*/,
        bool /*italic*/,
        bool /*bold*/) override
    {
        return std::nullopt;
    }
};

#endif // PREFIGURE_HAS_CAIRO

// ---------------------------------------------------------------------------
// liblouis braille translator (optional)
// ---------------------------------------------------------------------------

#ifdef PREFIGURE_HAS_LIBLOUIS

class LocalLouisBrailleTranslator : public AbstractBrailleTranslator {
public:
    LocalLouisBrailleTranslator();

    bool initialized() const override;

    std::optional<std::string> translate(
        const std::string& text,
        const std::vector<int>& typeform) override;

private:
    bool louis_loaded_ = false;
};

#else

/// Stub when liblouis is not available.
class LocalLouisBrailleTranslator : public AbstractBrailleTranslator {
public:
    bool initialized() const override { return false; }

    std::optional<std::string> translate(
        const std::string& /*text*/,
        const std::vector<int>& /*typeform*/) override
    {
        return std::nullopt;
    }
};

#endif // PREFIGURE_HAS_LIBLOUIS

}  // namespace prefigure
