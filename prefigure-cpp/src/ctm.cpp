#include "prefigure/ctm.hpp"

#include <spdlog/spdlog.h>

#include <cmath>
#include <format>

namespace prefigure {

// Helper: dot product of two 3-element arrays
static double dot3(const std::array<double, 3>& a, const std::array<double, 3>& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// --- Free affine matrix functions ---

AffineMatrix affine_identity() {
    return {{{1, 0, 0}, {0, 1, 0}}};
}

AffineMatrix affine_translation(double x, double y) {
    return {{{1, 0, x}, {0, 1, y}}};
}

AffineMatrix affine_scaling(double sx, double sy) {
    return {{{sx, 0, 0}, {0, sy, 0}}};
}

AffineMatrix affine_rotation(double theta, const std::string& units) {
    if (units == "deg") {
        theta *= M_PI / 180.0;
    }
    double c = std::cos(theta);
    double s = std::sin(theta);
    return {{{c, -s, 0}, {s, c, 0}}};
}

AffineMatrix affine_build_matrix(double m00, double m01, double m10, double m11) {
    return {{{m00, m01, 0}, {m10, m11, 0}}};
}

AffineMatrix affine_concat(const AffineMatrix& m, const AffineMatrix& n) {
    // Treat n as a 3x3 matrix (with implicit [0,0,1] bottom row)
    // c[col] = column of n transposed for dot products
    std::array<double, 3> c0 = {n[0][0], n[1][0], 0};
    std::array<double, 3> c1 = {n[0][1], n[1][1], 0};
    std::array<double, 3> c2 = {n[0][2], n[1][2], 1};

    return {{{dot3(m[0], c0), dot3(m[0], c1), dot3(m[0], c2)},
             {dot3(m[1], c0), dot3(m[1], c1), dot3(m[1], c2)}}};
}

// --- SVG transform string generators ---

std::string translatestr(double x, double y) {
    return std::format("translate({:.1f},{:.1f})", x, y);
}

std::string scalestr(double x, double y) {
    return std::format("scale({},{})", x, y);
}

std::string rotatestr(double theta) {
    return std::format("rotate({:.1f})", -theta);
}

std::string matrixstr(const AffineMatrix& m) {
    return std::format("matrix({:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f})",
                       m[0][0], -m[1][0], -m[0][1], m[1][1], 0.0, 0.0);
}

// --- CTM class ---

CTM::CTM()
    : ctm_(affine_identity())
    , inverse_(affine_identity())
    , scale_x_([](double x) { return x; })
    , scale_y_([](double y) { return y; })
    , inv_scale_x_([](double x) { return x; })
    , inv_scale_y_([](double y) { return y; })
{}

CTM::CTM(const AffineMatrix& ctm)
    : ctm_(ctm)
    , inverse_(affine_identity())  // caller must set inverse if needed
    , scale_x_([](double x) { return x; })
    , scale_y_([](double y) { return y; })
    , inv_scale_x_([](double x) { return x; })
    , inv_scale_y_([](double y) { return y; })
{}

void CTM::push() {
    stack_.emplace_back(ctm_, inverse_);
}

void CTM::pop() {
    if (stack_.empty()) {
        spdlog::error("Attempt to restore an empty transform");
        return;
    }
    auto [c, i] = stack_.back();
    ctm_ = c;
    inverse_ = i;
    stack_.pop_back();
}

void CTM::set_log_x() {
    scale_x_ = [](double x) { return std::log10(x); };
    inv_scale_x_ = [](double x) { return std::pow(10.0, x); };
}

void CTM::set_log_y() {
    scale_y_ = [](double y) { return std::log10(y); };
    inv_scale_y_ = [](double y) { return std::pow(10.0, y); };
}

void CTM::translate(double x, double y) {
    auto m = affine_translation(x, y);
    ctm_ = affine_concat(ctm_, m);
    auto minv = affine_translation(-x, -y);
    inverse_ = affine_concat(minv, inverse_);
}

void CTM::scale(double sx, double sy) {
    auto s = affine_scaling(sx, sy);
    ctm_ = affine_concat(ctm_, s);
    auto sinv = affine_scaling(1.0 / sx, 1.0 / sy);
    inverse_ = affine_concat(sinv, inverse_);
}

void CTM::rotate(double theta, const std::string& units) {
    auto m = affine_rotation(theta, units);
    ctm_ = affine_concat(ctm_, m);
    auto minv = affine_rotation(-theta, units);
    inverse_ = affine_concat(minv, inverse_);
}

void CTM::apply_matrix(double m00, double m01, double m10, double m11) {
    auto m = affine_build_matrix(m00, m01, m10, m11);
    ctm_ = affine_concat(ctm_, m);
    double det = m00 * m11 - m01 * m10;
    auto minv = affine_build_matrix(m11 / det, -m01 / det, -m10 / det, m00 / det);
    inverse_ = affine_concat(minv, inverse_);
}

Point2d CTM::transform(const Point2d& p) const {
    std::array<double, 3> pt = {scale_x_(p[0]), scale_y_(p[1]), 1.0};
    return Point2d(dot3(ctm_[0], pt), dot3(ctm_[1], pt));
}

Point2d CTM::inverse_transform(const Point2d& p) const {
    std::array<double, 3> pt = {p[0], p[1], 1.0};
    double ix = dot3(inverse_[0], pt);
    double iy = dot3(inverse_[1], pt);
    return Point2d(inv_scale_x_(ix), inv_scale_y_(iy));
}

CTM CTM::copy() const {
    CTM result;
    result.ctm_ = ctm_;
    result.inverse_ = inverse_;
    result.scale_x_ = scale_x_;
    result.scale_y_ = scale_y_;
    result.inv_scale_x_ = inv_scale_x_;
    result.inv_scale_y_ = inv_scale_y_;
    // Don't copy the stack — matches Python's deepcopy behavior
    return result;
}

// --- Transform element handlers ---
// These are stubs that will be fully implemented once Diagram and
// ExpressionContext are available. The signatures match the tag dispatcher.

void transform_group(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status) {
    // Will be implemented in Phase 2 when diagram.hpp is available
    (void)element; (void)diagram; (void)root; (void)status;
}

void transform_translate(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status) {
    (void)element; (void)diagram; (void)root; (void)status;
}

void transform_rotate(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status) {
    (void)element; (void)diagram; (void)root; (void)status;
}

void transform_scale(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status) {
    (void)element; (void)diagram; (void)root; (void)status;
}

}  // namespace prefigure
