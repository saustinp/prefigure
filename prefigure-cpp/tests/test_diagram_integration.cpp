#include <catch2/catch_test_macros.hpp>

#include <pugixml.hpp>

#include <filesystem>
#include <string>

// Integration tests will be filled in once Diagram is fully implemented.
// For now, just verify the test XML resources are accessible.

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

    // Verify it has a <diagram> element
    auto diagram = doc.child("diagram");
    REQUIRE(diagram);
    REQUIRE(diagram.attribute("dimensions"));
}
