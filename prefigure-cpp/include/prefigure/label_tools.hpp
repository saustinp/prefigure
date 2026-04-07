#pragma once

#include "types.hpp"

#include <pugixml.hpp>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace prefigure {

// ---------------------------------------------------------------------------
// Abstract interfaces
// ---------------------------------------------------------------------------

/**
 * @brief Abstract interface for math label processing (MathJax or browser-side).
 *
 * @details Provides a batch-processing pipeline for LaTeX math expressions:
 * 1. Register macros via add_macros().
 * 2. Queue individual labels via register_math_label().
 * 3. Run the batch processor via process_math_labels().
 * 4. Retrieve results via get_math_label() (SVG) or get_math_braille() (tactile).
 *
 * Concrete implementations include LocalMathLabels (desktop, Node.js + MathJax)
 * and potentially browser-side implementations for Pyodide environments.
 *
 * @see LocalMathLabels
 */
class AbstractMathLabels {
public:
    virtual ~AbstractMathLabels() = default;

    /**
     * @brief Register LaTeX macro definitions for use in subsequent labels.
     * @param macros A string of LaTeX macro definitions.
     */
    virtual void add_macros(const std::string& macros) = 0;

    /**
     * @brief Queue a math expression for batch processing.
     *
     * @param id   A unique identifier for this label (used to retrieve the result).
     * @param text The LaTeX math expression to render.
     *
     * @see process_math_labels(), get_math_label()
     */
    virtual void register_math_label(const std::string& id, const std::string& text) = 0;

    /**
     * @brief Run the batch processor (MathJax, etc.) over all queued labels.
     *
     * @details For LocalMathLabels, this shells out to a Node.js process running
     * MathJax to convert all queued LaTeX expressions into SVG fragments or
     * braille text in a single batch operation.
     *
     * @note Must be called after all labels are registered and before any
     *       get_math_label() or get_math_braille() calls.
     */
    virtual void process_math_labels() = 0;

    /**
     * @brief Retrieve the processed SVG fragment for a math label.
     *
     * @param id The unique identifier used during registration.
     * @return An XML node containing the rendered SVG math content.
     *         Returns a null node for tactile format (use get_math_braille() instead).
     *
     * @see register_math_label(), process_math_labels()
     */
    virtual XmlNode get_math_label(const std::string& id) = 0;

    /**
     * @brief Retrieve the braille text for a tactile math label.
     *
     * @param id The unique identifier used during registration.
     * @return The braille Unicode string for the math expression.
     *
     * @see register_math_label(), process_math_labels()
     */
    virtual std::string get_math_braille(const std::string& id) = 0;
};

/**
 * @brief Abstract interface for measuring text dimensions.
 *
 * @details Used during label placement to determine the width and height of
 * rendered text without actually rendering it. This information is needed to
 * compute label alignment offsets.
 *
 * @see CairoTextMeasurements
 */
class AbstractTextMeasurements {
public:
    virtual ~AbstractTextMeasurements() = default;

    /**
     * @brief Measure the dimensions of a text string with the given font properties.
     *
     * @param text   The text string to measure.
     * @param family The font family name (e.g., "serif", "sans-serif").
     * @param size   The font size in points.
     * @param italic Whether the text is italic.
     * @param bold   Whether the text is bold.
     * @return An array of {x_advance, above_baseline, below_baseline} in points,
     *         or std::nullopt if measurement is not available.
     */
    virtual std::optional<std::array<double, 3>> measure_text(
        const std::string& text,
        const std::string& family,
        double size,
        bool italic,
        bool bold) = 0;
};

/**
 * @brief Abstract interface for translating text to braille.
 *
 * @details Used for tactile output to convert label text into braille Unicode
 * strings. Supports typeform indicators for italic and bold formatting.
 *
 * @see LocalLouisBrailleTranslator
 */
class AbstractBrailleTranslator {
public:
    virtual ~AbstractBrailleTranslator() = default;

    /**
     * @brief Check whether the underlying braille library was successfully loaded.
     * @return True if translation is available, false otherwise.
     */
    virtual bool initialized() const = 0;

    /**
     * @brief Translate a text string to braille.
     *
     * @param text     The text to translate.
     * @param typeform A vector of per-character typeform indicators:
     *                 0 = plain, 1 = italic, 4 = bold. Must have the same
     *                 length as @p text.
     * @return The braille Unicode string, or std::nullopt if translation fails.
     */
    virtual std::optional<std::string> translate(
        const std::string& text,
        const std::vector<int>& typeform) = 0;
};

// ---------------------------------------------------------------------------
// Local (desktop) implementations
// ---------------------------------------------------------------------------

/**
 * @brief MathJax-based math label processor that shells out to Node.js.
 *
 * @details Builds an HTML document containing all queued math expressions,
 * writes it to a temporary file, invokes Node.js with a MathJax script to
 * convert the expressions, then parses the resulting SVG or braille output.
 *
 * For SVG format, each processed label becomes an SVG XML fragment that can
 * be inserted directly into the diagram's SVG tree.
 *
 * For tactile format, each label is converted to braille text and cached
 * for retrieval via get_math_braille().
 *
 * @see AbstractMathLabels
 */
class LocalMathLabels : public AbstractMathLabels {
public:
    /**
     * @brief Construct a LocalMathLabels processor for the given output format.
     * @param format SVG or Tactile output mode.
     */
    explicit LocalMathLabels(OutputFormat format);

    /** @copydoc AbstractMathLabels::add_macros() */
    void add_macros(const std::string& macros) override;

    /** @copydoc AbstractMathLabels::register_math_label() */
    void register_math_label(const std::string& id, const std::string& text) override;

    /** @copydoc AbstractMathLabels::process_math_labels() */
    void process_math_labels() override;

    /** @copydoc AbstractMathLabels::get_math_label() */
    XmlNode get_math_label(const std::string& id) override;

    /** @copydoc AbstractMathLabels::get_math_braille() */
    std::string get_math_braille(const std::string& id) override;

    // Internal accessors used by the on-disk cache atexit hook in
    // label_tools.cpp.  Not part of the public API.
    static const std::unordered_map<std::string, std::string>&
        s_svg_cache_for_atexit();
    static const std::unordered_map<std::string, std::string>&
        s_braille_cache_for_atexit();

private:
    // Try to render every entry in missing_ids_ via the persistent
    // MathJax daemon.  Returns true on full success (caches updated),
    // false on any failure (caller falls through to the legacy one-shot
    // node mj-sre-page.js path so a broken daemon never breaks builds).
    bool try_render_via_daemon();
    OutputFormat format_;
    bool labels_present_ = false;

    // HTML document built up for MathJax input
    pugi::xml_document html_doc_;
    pugi::xml_node html_body_;

    // Parsed MathJax output (only populated on a cache miss)
    pugi::xml_document label_tree_;
    bool label_tree_valid_ = false;

    // Per-instance braille text cache, keyed by label id (legacy, kept
    // for compatibility with the existing cache-miss xpath fallback path)
    std::unordered_map<std::string, std::string> braille_cache_;

    // -- Process-lifetime LaTeX -> rendered cache ---------------------------
    //
    // The Node.js MathJax invocation in process_math_labels() costs ~700 ms
    // per call (Node startup + V8 boot + MathJax library load + LaTeX
    // typesetting), and that cost dominates wall-clock build time for any
    // diagram with math labels.  Since the LaTeX-to-rendered-SVG mapping is
    // a pure function of the input string and the (fixed) installed MathJax
    // version, we cache the result keyed by LaTeX text and skip the
    // subprocess entirely when every label in a build is already cached.
    //
    // The caches are *static* so they persist across LocalMathLabels
    // instance lifetimes (label.cpp::init() recreates the instance on every
    // diagram build, but the same Python process can build many diagrams).
    //
    // s_svg_cache_:     LaTeX -> serialised <svg>...</svg> (sighted output)
    // s_braille_cache_: LaTeX -> braille Unicode string (tactile output)
    //
    // String form is used for the SVG cache rather than a pre-parsed
    // document because (a) it makes ownership trivial and (b) the consumer
    // (mk_m_element in label.cpp) mutates the returned node in place, so we
    // have to re-parse on every get anyway.  Parsing 200-byte snippets is
    // microseconds, orders of magnitude cheaper than re-running MathJax.
    static std::unordered_map<std::string, std::string> s_svg_cache_;
    static std::unordered_map<std::string, std::string> s_braille_cache_;

    // Per-instance: id -> LaTeX text, used to look up the cache by content
    // for each labelled element after process_math_labels() has run.
    std::unordered_map<std::string, std::string> id_to_text_;

    // Per-instance: ids that missed the cache and were sent through the
    // MathJax subprocess in this build.  Used at get-time to fall back to
    // the legacy xpath-on-label_tree_ path if needed (defensive).
    std::unordered_set<std::string> missing_ids_;

    // Per-instance: pugixml documents owning the parsed cache hits returned
    // from get_math_label().  Each call to get_math_label() parses the
    // cached string into a fresh document, pushes it onto this vector, and
    // returns the root <svg> node from that document.  The documents stay
    // alive until the LocalMathLabels instance is destroyed (i.e., until
    // the next diagram build calls label::init()), which is long enough for
    // the caller to deep-copy the returned node into the diagram tree.
    std::vector<std::unique_ptr<pugi::xml_document>> get_docs_;

    // Per-instance counter used to suffix glyph <defs> ids on every cache
    // hit.  MathJax assigns sequence-numbered ids like "MJX-1-TEX-N-31"
    // within a single render, and mk_m_element later rewrites those ids by
    // prepending the diagram-id prefix.  If two labels in the same build
    // share the same LaTeX text, they would both come out of the cache with
    // identical glyph ids and produce duplicate ids in the output document.
    // By appending a fresh "-r{N}" suffix on every get, we keep every
    // emitted glyph id unique within the build.  Per-instance (not static)
    // so every build gets a fresh sequence starting at 1, which makes
    // warm-cache output byte-identical to cold-cache output for diagrams
    // without repeated labels.
    int render_counter_ = 0;
};

// ---------------------------------------------------------------------------
// Cairo text measurement (optional)
// ---------------------------------------------------------------------------

#ifdef PREFIGURE_HAS_CAIRO

/**
 * @brief Text measurement implementation using the Cairo graphics library.
 *
 * @details Uses Cairo's font rendering engine to measure the advance width,
 * ascent (above baseline), and descent (below baseline) of text strings.
 * This provides accurate measurements for font metrics used in label placement.
 *
 * @note Only available when compiled with PREFIGURE_HAS_CAIRO.
 *
 * @see AbstractTextMeasurements
 */
class CairoTextMeasurements : public AbstractTextMeasurements {
public:
    /** @brief Construct the Cairo measurement backend and initialize a Cairo surface. */
    CairoTextMeasurements();

    /** @brief Destructor (releases Cairo resources). */
    ~CairoTextMeasurements() override;

    /** @copydoc AbstractTextMeasurements::measure_text() */
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

/**
 * @brief Stub text measurement backend when Cairo is not available.
 *
 * @details Always returns std::nullopt for all measurements. Label placement
 * will fall back to estimated dimensions when this stub is active.
 *
 * @see AbstractTextMeasurements
 */
class CairoTextMeasurements : public AbstractTextMeasurements {
public:
    /** @copydoc AbstractTextMeasurements::measure_text() */
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

/**
 * @brief Braille translation implementation using the liblouis library.
 *
 * @details Wraps the liblouis C library to translate text strings into
 * braille Unicode characters. Supports typeform indicators for italic
 * and bold formatting. Uses the en-ueb-g2.ctb (English Unified English
 * Braille Grade 2) translation table.
 *
 * @note Only available when compiled with PREFIGURE_HAS_LIBLOUIS.
 *
 * @see AbstractBrailleTranslator
 */
class LocalLouisBrailleTranslator : public AbstractBrailleTranslator {
public:
    /**
     * @brief Construct the translator and attempt to load the liblouis library.
     * @note Check initialized() to determine if loading succeeded.
     */
    LocalLouisBrailleTranslator();

    /** @copydoc AbstractBrailleTranslator::initialized() */
    bool initialized() const override;

    /** @copydoc AbstractBrailleTranslator::translate() */
    std::optional<std::string> translate(
        const std::string& text,
        const std::vector<int>& typeform) override;

private:
    bool louis_loaded_ = false;
};

#else

/**
 * @brief Stub braille translator when liblouis is not available.
 *
 * @details Always reports as uninitialized and returns std::nullopt for
 * all translation requests.
 *
 * @see AbstractBrailleTranslator
 */
class LocalLouisBrailleTranslator : public AbstractBrailleTranslator {
public:
    /** @copydoc AbstractBrailleTranslator::initialized() */
    bool initialized() const override { return false; }

    /** @copydoc AbstractBrailleTranslator::translate() */
    std::optional<std::string> translate(
        const std::string& /*text*/,
        const std::vector<int>& /*typeform*/) override
    {
        return std::nullopt;
    }
};

#endif // PREFIGURE_HAS_LIBLOUIS

}  // namespace prefigure
