#pragma once

#include "types.hpp"

#include <Eigen/Dense>
#include <pugixml.hpp>

#include <string>
#include <unordered_map>

namespace prefigure {

// Color name lookup and normalization
std::string get_color(const std::string& color);

// XML attribute helpers
void add_attr(XmlNode element, const std::unordered_map<std::string, std::string>& attrs);
std::string get_attr(XmlNode element, const std::string& attr, const std::string& default_val);
void set_attr(XmlNode element, const std::string& attr, const std::string& default_val);

// SVG styling attribute extraction
// get_1d_attr: stroke, thickness, dash, etc. (fill defaults to 'none')
std::unordered_map<std::string, std::string> get_1d_attr(XmlNode element);
// get_2d_attr: like get_1d_attr but includes fill color
std::unordered_map<std::string, std::string> get_2d_attr(XmlNode element);

// Apply cliptobbox if the element requests it
void cliptobbox(XmlNode g_element, XmlNode element, Diagram& diagram);

// Float to string formatting (matching Python's "%.1f" and "%.4f")
std::string float2str(double x);
std::string float2longstr(double x);

// Point to string formatting
std::string pt2str(const Point2d& p, const std::string& spacer = " ", bool paren = false);
std::string pt2str(const Eigen::VectorXd& p, const std::string& spacer = " ", bool paren = false);
std::string pt2long_str(const Point2d& p, const std::string& spacer = " ");

// Numpy-style string: "(x,y)"
std::string np2str(const Point2d& p);

}  // namespace prefigure
