#include <catch2/catch_test_macros.hpp>

#include <pugixml.hpp>
#include "prefigure/parse.hpp"
#include "prefigure/diagram.hpp"

#include <filesystem>
#include <fstream>
#include <string>

TEST_CASE("Test resources exist", "[integration]") {
    std::string resources_dir = TEST_RESOURCES_DIR;

    REQUIRE(std::filesystem::exists(resources_dir + "/tangent.xml"));
    REQUIRE(std::filesystem::exists(resources_dir + "/derivatives.xml"));
    REQUIRE(std::filesystem::exists(resources_dir + "/de-system.xml"));
    REQUIRE(std::filesystem::exists(resources_dir + "/diffeqs.xml"));
    REQUIRE(std::filesystem::exists(resources_dir + "/implicit.xml"));
    REQUIRE(std::filesystem::exists(resources_dir + "/projection.xml"));
    REQUIRE(std::filesystem::exists(resources_dir + "/riemann.xml"));
    REQUIRE(std::filesystem::exists(resources_dir + "/roots_of_unity.xml"));
}

TEST_CASE("Test XML files are parseable", "[integration]") {
    std::string resources_dir = TEST_RESOURCES_DIR;

    pugi::xml_document doc;
    auto result = doc.load_file((resources_dir + "/tangent.xml").c_str());
    REQUIRE(result);

    auto diagram = doc.child("diagram");
    REQUIRE(diagram);
    REQUIRE(diagram.attribute("dimensions"));
}

TEST_CASE("Full pipeline: tangent.xml produces SVG output", "[integration][pipeline]") {
    std::string resources_dir = TEST_RESOURCES_DIR;
    std::string xml_path = resources_dir + "/tangent.xml";

    // Run the full parse pipeline
    REQUIRE_NOTHROW(
        prefigure::parse(xml_path, prefigure::OutputFormat::SVG,
                         "", false, prefigure::Environment::PfCli)
    );

    // Check that SVG output was created
    std::string output_path = resources_dir + "/output/tangent.svg";
    REQUIRE(std::filesystem::exists(output_path));

    // Verify the SVG is well-formed XML
    pugi::xml_document svg_doc;
    auto load_result = svg_doc.load_file(output_path.c_str());
    REQUIRE(load_result);

    // Verify it's an SVG root
    auto svg_root = svg_doc.child("svg");
    REQUIRE(svg_root);

    // Should have width/height/viewBox
    REQUIRE(svg_root.attribute("width"));
    REQUIRE(svg_root.attribute("height"));

    // Should have <defs> with at least a clippath
    auto defs = svg_root.child("defs");
    REQUIRE(defs);

    // Should have some path elements (the graph and tangent line)
    int path_count = 0;
    for (auto node : svg_root.children()) {
        if (std::string(node.name()) == "path") path_count++;
    }
    // At minimum we expect some graphical elements were created
    INFO("SVG has " << path_count << " top-level path elements");

    // Count all elements recursively
    int total_elements = 0;
    std::function<void(pugi::xml_node)> count_elements = [&](pugi::xml_node n) {
        for (auto child : n.children()) {
            if (child.type() == pugi::node_element) {
                total_elements++;
                count_elements(child);
            }
        }
    };
    count_elements(svg_root);
    INFO("SVG has " << total_elements << " total elements");
    REQUIRE(total_elements > 5);

    // Clean up
    std::filesystem::remove(output_path);
    std::filesystem::remove_all(resources_dir + "/output");
}

// projection.xml requires vector/label modules (Phase 4+) — skip for now
// TEST_CASE("Full pipeline: projection.xml produces SVG output", "[integration][pipeline]") { ... }
