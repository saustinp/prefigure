#pragma once

#include "types.hpp"

#include <Eigen/Dense>

#include <functional>
#include <string>
#include <vector>

namespace prefigure {

// 2x3 affine matrix operations (list-of-lists representation for compatibility)
// The matrix [[a,b,c],[d,e,f]] maps (x,y) → (ax+by+c, dx+ey+f)

using AffineMatrix = std::array<std::array<double, 3>, 2>;

AffineMatrix affine_identity();
AffineMatrix affine_translation(double x, double y);
AffineMatrix affine_scaling(double sx, double sy);
AffineMatrix affine_rotation(double theta, const std::string& units = "deg");
AffineMatrix affine_build_matrix(double m00, double m01, double m10, double m11);
AffineMatrix affine_concat(const AffineMatrix& m, const AffineMatrix& n);

// SVG transform string generators
std::string translatestr(double x, double y);
std::string scalestr(double x, double y);
std::string rotatestr(double theta);
std::string matrixstr(const AffineMatrix& m);

// Current Transformation Matrix class
class CTM {
public:
    CTM();
    explicit CTM(const AffineMatrix& ctm);
    CTM(const CTM& other) = default;
    CTM& operator=(const CTM& other) = default;

    void push();
    void pop();

    void translate(double x, double y);
    void scale(double sx, double sy);
    void rotate(double theta, const std::string& units = "deg");
    void apply_matrix(double m00, double m01, double m10, double m11);

    void set_log_x();
    void set_log_y();

    Point2d transform(const Point2d& p) const;
    Point2d inverse_transform(const Point2d& p) const;

    CTM copy() const;

    const AffineMatrix& get_ctm() const { return ctm_; }
    const AffineMatrix& get_inverse() const { return inverse_; }

private:
    AffineMatrix ctm_;
    AffineMatrix inverse_;
    std::vector<std::pair<AffineMatrix, AffineMatrix>> stack_;

    std::function<double(double)> scale_x_;
    std::function<double(double)> scale_y_;
    std::function<double(double)> inv_scale_x_;
    std::function<double(double)> inv_scale_y_;
};

// Transform element handlers (registered in tags dispatcher)
void transform_group(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status);
void transform_translate(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status);
void transform_rotate(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status);
void transform_scale(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status);

}  // namespace prefigure
