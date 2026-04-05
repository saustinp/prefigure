#pragma once

#include "types.hpp"

#include <string>

namespace prefigure {

// Abstract interfaces for label rendering backends
class LabelRenderer {
public:
    virtual ~LabelRenderer() = default;
};

class MathJaxRenderer : public LabelRenderer {
public:
    ~MathJaxRenderer() override = default;
};

class TactileLabelRenderer : public LabelRenderer {
public:
    ~TactileLabelRenderer() override = default;
};

}  // namespace prefigure
