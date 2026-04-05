#pragma once

#include "types.hpp"

#include <Eigen/Dense>

#include <functional>
#include <string>
#include <vector>

namespace prefigure {

/**
 * @brief 2x3 affine transformation matrix type.
 *
 * Stored as `[[a, b, c], [d, e, f]]` which maps a point (x, y) to:
 *   x' = a*x + b*y + c
 *   y' = d*x + e*y + f
 *
 * The implicit third row is [0, 0, 1].
 */
using AffineMatrix = std::array<std::array<double, 3>, 2>;

/**
 * @brief Return the 2x3 identity affine matrix.
 * @return [[1,0,0],[0,1,0]]
 */
AffineMatrix affine_identity();

/**
 * @brief Return a 2x3 translation matrix.
 * @param x Horizontal displacement.
 * @param y Vertical displacement.
 * @return [[1,0,x],[0,1,y]]
 */
AffineMatrix affine_translation(double x, double y);

/**
 * @brief Return a 2x3 scaling matrix.
 * @param sx Horizontal scale factor.
 * @param sy Vertical scale factor.
 * @return [[sx,0,0],[0,sy,0]]
 */
AffineMatrix affine_scaling(double sx, double sy);

/**
 * @brief Return a 2x3 rotation matrix.
 * @param theta Rotation angle.
 * @param units Angle units: "deg" (default) or "rad".
 * @return The rotation matrix for counter-clockwise rotation by @p theta.
 */
AffineMatrix affine_rotation(double theta, const std::string& units = "deg");

/**
 * @brief Build a 2x3 affine matrix from a 2x2 linear part (no translation).
 * @param m00 Element (0,0).
 * @param m01 Element (0,1).
 * @param m10 Element (1,0).
 * @param m11 Element (1,1).
 * @return [[m00,m01,0],[m10,m11,0]]
 */
AffineMatrix affine_build_matrix(double m00, double m01, double m10, double m11);

/**
 * @brief Concatenate two affine matrices: result = m * n.
 *
 * Applies transformation @p n first, then @p m.  This is standard
 * matrix composition with the implicit [0,0,1] third row.
 *
 * @param m The outer (left) transformation.
 * @param n The inner (right) transformation.
 * @return The composed affine matrix.
 */
AffineMatrix affine_concat(const AffineMatrix& m, const AffineMatrix& n);

/**
 * @brief Generate an SVG `translate(x,y)` transform string.
 * @param x Horizontal translation.
 * @param y Vertical translation.
 * @return A string like "translate(1.0,2.0)".
 */
std::string translatestr(double x, double y);

/**
 * @brief Generate an SVG `scale(x,y)` transform string.
 * @param x Horizontal scale factor.
 * @param y Vertical scale factor.
 * @return A string like "scale(1,2)".
 */
std::string scalestr(double x, double y);

/**
 * @brief Generate an SVG `rotate(angle)` transform string.
 *
 * @param theta Rotation angle in degrees.
 * @return A string like "rotate(-45.0)".
 *
 * @note The angle is negated for SVG's clockwise-positive convention.
 */
std::string rotatestr(double theta);

/**
 * @brief Generate an SVG `matrix(a,b,c,d,e,f)` transform string from an AffineMatrix.
 *
 * Maps the 2x3 affine matrix to SVG's matrix(a,b,c,d,e,f) format, with
 * sign adjustments for SVG's inverted y-axis convention.
 *
 * @param m The affine matrix.
 * @return A string like "matrix(1.0000,0.0000,0.0000,1.0000,0.0000,0.0000)".
 */
std::string matrixstr(const AffineMatrix& m);

/**
 * @brief Current Transformation Matrix for converting between user and SVG coordinates.
 *
 * The CTM maintains both the forward transformation (user -> SVG) and its
 * inverse (SVG -> user).  It supports an internal save/restore stack for
 * nested coordinate systems, and optional logarithmic axis scaling.
 *
 * @par Transformation pipeline
 * When transform() is called on a point (x, y):
 * 1. Apply scale functions: x' = scale_x_(x), y' = scale_y_(y)
 * 2. Apply the affine matrix: result = ctm_ * [x', y', 1]^T
 *
 * @see Diagram::ctm() for obtaining the active CTM.
 */
class CTM {
public:
    /**
     * @brief Construct an identity CTM with linear (identity) scale functions.
     */
    CTM();

    /**
     * @brief Construct a CTM from an existing affine matrix.
     *
     * The inverse is initialized to identity; the caller must set it separately
     * if inverse_transform() will be used.
     *
     * @param ctm The initial forward affine matrix.
     */
    explicit CTM(const AffineMatrix& ctm);

    CTM(const CTM& other) = default;
    CTM& operator=(const CTM& other) = default;

    /**
     * @brief Save the current forward and inverse matrices onto the internal stack.
     * @see pop()
     */
    void push();

    /**
     * @brief Restore the most recently saved matrices from the internal stack.
     *
     * @note Logs an error and does nothing if the stack is empty.
     * @see push()
     */
    void pop();

    /**
     * @brief Post-multiply a translation into the CTM.
     * @param x Horizontal displacement in user coordinates.
     * @param y Vertical displacement in user coordinates.
     */
    void translate(double x, double y);

    /**
     * @brief Post-multiply a scaling into the CTM.
     * @param sx Horizontal scale factor.
     * @param sy Vertical scale factor.
     */
    void scale(double sx, double sy);

    /**
     * @brief Post-multiply a rotation into the CTM.
     * @param theta Rotation angle.
     * @param units Angle units: "deg" (default) or "rad".
     */
    void rotate(double theta, const std::string& units = "deg");

    /**
     * @brief Post-multiply an arbitrary 2x2 linear transformation into the CTM.
     *
     * The inverse is computed using the analytical formula for 2x2 matrix inversion.
     *
     * @param m00 Element (0,0).
     * @param m01 Element (0,1).
     * @param m10 Element (1,0).
     * @param m11 Element (1,1).
     */
    void apply_matrix(double m00, double m01, double m10, double m11);

    /**
     * @brief Enable logarithmic (base-10) scaling on the x-axis.
     *
     * After calling this, transform() will apply log10 to the x-coordinate
     * before the affine matrix, and inverse_transform() will apply 10^x
     * after the inverse matrix.
     */
    void set_log_x();

    /**
     * @brief Enable logarithmic (base-10) scaling on the y-axis.
     *
     * @see set_log_x() for details.
     */
    void set_log_y();

    /**
     * @brief Transform a point from user coordinates to SVG coordinates.
     *
     * Applies the scale functions and then the forward affine matrix.
     *
     * @param p Point in user (mathematical) coordinates.
     * @return The transformed point in SVG pixel coordinates.
     */
    Point2d transform(const Point2d& p) const;

    /**
     * @brief Transform a point from SVG coordinates back to user coordinates.
     *
     * Applies the inverse affine matrix and then the inverse scale functions.
     *
     * @param p Point in SVG pixel coordinates.
     * @return The corresponding point in user (mathematical) coordinates.
     */
    Point2d inverse_transform(const Point2d& p) const;

    /**
     * @brief Create a deep copy of this CTM.
     *
     * Copies the matrices and scale functions but not the internal save/restore
     * stack, matching the Python implementation's deepcopy behavior.
     *
     * @return A new CTM with the same transformation state.
     */
    CTM copy() const;

    /**
     * @brief Get a const reference to the forward affine matrix.
     * @return The current forward 2x3 matrix.
     */
    const AffineMatrix& get_ctm() const { return ctm_; }

    /**
     * @brief Get a const reference to the inverse affine matrix.
     * @return The current inverse 2x3 matrix.
     */
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

/**
 * @brief Render a `<transform>` XML element as SVG.
 *
 * Treated as a `<group>` element with a `@transform` attribute.
 * Delegates directly to group().
 *
 * @par XML Attributes
 * - `transform` (optional): SVG transform string applied to the group.
 *
 * @par SVG Output
 * Creates a `<g>` with the specified transform.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param root    SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see group()
 */
void transform_group(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status);

/**
 * @brief Render a `<translate>` XML element as SVG.
 *
 * Constructs a `translate(...)` SVG transform string from the `@by`
 * attribute and delegates to group().
 *
 * @par XML Attributes
 * - `by` (required): Translation vector, e.g., "1,2".
 *
 * @par SVG Output
 * Creates a `<g>` with `transform="translate(1,2)"`.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param root    SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see group()
 */
void transform_translate(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status);

/**
 * @brief Render a `<rotate>` XML element as SVG.
 *
 * Constructs a `rotate(...)` SVG transform string from the `@by`
 * attribute and delegates to group().
 *
 * @par XML Attributes
 * - `by` (required): Rotation angle string.
 *
 * @par SVG Output
 * Creates a `<g>` with `transform="rotate(...)"`.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param root    SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see group()
 */
void transform_rotate(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status);

/**
 * @brief Render a `<scale>` XML element as SVG.
 *
 * Constructs a `scale(...)` SVG transform string from the `@by`
 * attribute and delegates to group().
 *
 * @par XML Attributes
 * - `by` (required): Scale factors string.
 *
 * @par SVG Output
 * Creates a `<g>` with `transform="scale(...)"`.
 *
 * @param element Source XML element.
 * @param diagram Parent diagram context.
 * @param root    SVG parent node for appending output.
 * @param status  Outline rendering pass.
 *
 * @see group()
 */
void transform_scale(XmlNode element, Diagram& diagram, XmlNode root, OutlineStatus status);

}  // namespace prefigure
