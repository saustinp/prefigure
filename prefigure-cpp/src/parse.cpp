#include "prefigure/parse.hpp"
#include "prefigure/diagram.hpp"

namespace prefigure {

Diagram mk_diagram(const std::string& source,
                   const std::string& filename,
                   std::optional<int> diagram_number,
                   OutputFormat format,
                   std::optional<std::string> output,
                   XmlNode publication,
                   bool suppress_caption,
                   Environment environment) {
    // Parse the XML source
    static XmlDoc doc;
    doc.load_string(source.c_str());
    XmlNode root = doc.document_element();

    return Diagram(root, filename, diagram_number, format,
                   output, publication, suppress_caption, environment);
}

void parse(const std::string& source,
           const std::string& filename,
           std::optional<int> diagram_number,
           OutputFormat format,
           std::optional<std::string> output,
           XmlNode publication,
           bool suppress_caption,
           Environment environment) {
    auto diagram = mk_diagram(source, filename, diagram_number, format,
                              output, publication, suppress_caption, environment);
    diagram.begin_figure();
    diagram.parse();
    diagram.place_labels();
    diagram.end_figure();
}

}  // namespace prefigure
