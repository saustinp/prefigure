#include <catch2/catch_test_macros.hpp>

#include <pugixml.hpp>
#include "prefigure/parse.hpp"
#include "prefigure/diagram.hpp"

#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

static const std::string RES = TEST_RESOURCES_DIR;

// Helper: count all elements recursively in an XML tree
static int count_elements(pugi::xml_node root) {
    int total = 0;
    std::function<void(pugi::xml_node)> walk = [&](pugi::xml_node n) {
        for (auto child : n.children()) {
            if (child.type() == pugi::node_element) {
                total++;
                walk(child);
            }
        }
    };
    walk(root);
    return total;
}

// Helper: run full pipeline on an XML file and verify SVG output
static void verify_pipeline(const std::string& name, int min_elements = 5) {
    std::string xml_path = RES + "/" + name + ".xml";
    std::string output_path = RES + "/output/" + name + ".svg";

    // Run the full parse pipeline
    REQUIRE_NOTHROW(
        prefigure::parse(xml_path, prefigure::OutputFormat::SVG,
                         "", false, prefigure::Environment::PfCli)
    );

    // Check that SVG output was created
    REQUIRE(std::filesystem::exists(output_path));

    // Verify the SVG is well-formed XML
    pugi::xml_document svg_doc;
    auto load_result = svg_doc.load_file(output_path.c_str());
    REQUIRE(load_result);

    // Verify it's an SVG root with dimensions
    auto svg_root = svg_doc.child("svg");
    REQUIRE(svg_root);
    REQUIRE(svg_root.attribute("width"));
    REQUIRE(svg_root.attribute("height"));

    // Should have <defs>
    REQUIRE(svg_root.child("defs"));

    // Count elements
    int total = count_elements(svg_root);
    INFO(name << ".xml produced SVG with " << total << " elements");
    REQUIRE(total >= min_elements);

    // Clean up
    std::filesystem::remove(output_path);
    std::filesystem::remove_all(RES + "/output");
}

// --- Resource existence tests ---

TEST_CASE("Test resources exist", "[integration]") {
    REQUIRE(std::filesystem::exists(RES + "/tangent.xml"));
    REQUIRE(std::filesystem::exists(RES + "/derivatives.xml"));
    REQUIRE(std::filesystem::exists(RES + "/de-system.xml"));
    REQUIRE(std::filesystem::exists(RES + "/diffeqs.xml"));
    REQUIRE(std::filesystem::exists(RES + "/implicit.xml"));
    REQUIRE(std::filesystem::exists(RES + "/projection.xml"));
    REQUIRE(std::filesystem::exists(RES + "/riemann.xml"));
    REQUIRE(std::filesystem::exists(RES + "/roots_of_unity.xml"));
}

TEST_CASE("Test XML files are parseable", "[integration]") {
    pugi::xml_document doc;
    REQUIRE(doc.load_file((RES + "/tangent.xml").c_str()));
    auto diagram = doc.child("diagram");
    REQUIRE(diagram);
    REQUIRE(diagram.attribute("dimensions"));
}

// --- Full pipeline tests ---

TEST_CASE("Pipeline: tangent.xml", "[pipeline]") {
    verify_pipeline("tangent", 10);
}

TEST_CASE("Pipeline: derivatives.xml", "[pipeline]") {
    verify_pipeline("derivatives", 10);
}

TEST_CASE("Pipeline: implicit.xml", "[pipeline]") {
    verify_pipeline("implicit", 5);
}

TEST_CASE("Pipeline: projection.xml", "[pipeline]") {
    verify_pipeline("projection", 5);
}

TEST_CASE("Pipeline: riemann.xml", "[pipeline]") {
    verify_pipeline("riemann", 5);
}

TEST_CASE("Pipeline: roots_of_unity.xml", "[pipeline]") {
    verify_pipeline("roots_of_unity", 5);
}

// de-system.xml and diffeqs.xml require ODE solving (Phase 6) — test if available
#ifdef PREFIGURE_HAS_DIFFEQS
TEST_CASE("Pipeline: de-system.xml", "[pipeline][diffeqs]") {
    verify_pipeline("de-system", 5);
}

TEST_CASE("Pipeline: diffeqs.xml", "[pipeline][diffeqs]") {
    verify_pipeline("diffeqs", 5);
}
#endif
