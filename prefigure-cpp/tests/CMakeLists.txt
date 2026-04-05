# Copy test resources to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/golden
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

# Unit + integration tests
add_executable(prefigure_tests
    test_calculus.cpp
    test_math_utilities.cpp
    test_ctm.cpp
    test_user_namespace.cpp
    test_diagram_integration.cpp
    test_golden.cpp
)

target_link_libraries(prefigure_tests PRIVATE
    prefigure
    Catch2::Catch2WithMain
)

target_compile_definitions(prefigure_tests PRIVATE
    TEST_RESOURCES_DIR="${CMAKE_CURRENT_BINARY_DIR}/resources"
    TEST_GOLDEN_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden"
)

include(CTest)
include(Catch2::Catch2WithMain OPTIONAL)

if(COMMAND catch_discover_tests)
    catch_discover_tests(prefigure_tests)
else()
    add_test(NAME prefigure_tests COMMAND prefigure_tests)
endif()
