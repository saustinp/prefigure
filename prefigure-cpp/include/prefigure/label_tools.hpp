#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

/**
 * @brief Abstract base class for label rendering backends.
 *
 * Provides the interface for rendering text labels into SVG elements.
 * Concrete implementations handle different rendering strategies
 * (MathJax for visual SVG, tactile renderers for embossed output).
 *
 * @see MathJaxRenderer, TactileLabelRenderer
 */
class LabelRenderer {
public:
    /** @brief Virtual destructor for proper polymorphic cleanup. */
    virtual ~LabelRenderer() = default;
};

/**
 * @brief Label renderer that uses MathJax for LaTeX-to-SVG conversion.
 *
 * Renders mathematical labels by invoking MathJax to convert LaTeX
 * expressions into SVG path elements.  Used for standard (non-tactile)
 * SVG output.
 *
 * @see LabelRenderer
 */
class MathJaxRenderer : public LabelRenderer {
public:
    /** @brief Destructor. */
    ~MathJaxRenderer() override = default;
};

/**
 * @brief Label renderer for tactile/embossed output.
 *
 * Renders labels using Braille-compatible text or other tactile-friendly
 * representations suitable for embossed graphics.
 *
 * @see LabelRenderer
 */
class TactileLabelRenderer : public LabelRenderer {
public:
    /** @brief Destructor. */
    ~TactileLabelRenderer() override = default;
};

}  // namespace prefigure
