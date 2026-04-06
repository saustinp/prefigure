cmake_minimum_required(VERSION 3.24)
project(prefigure VERSION 0.6.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(PREFIGURE_BUILD_PYTHON "Build Python bindings via PyBind11" ON)
option(PREFIGURE_BUILD_TESTS "Build test suite" ON)
option(PREFIGURE_WITH_DIFFEQS "ODE solver support (Boost.Odeint)" ON)
option(PREFIGURE_WITH_NETWORK "Network graph support (Boost.Graph)" ON)
option(PREFIGURE_WITH_SHAPES "Boolean geometry support (GEOS)" ON)
option(PREFIGURE_WITH_CAIRO "Cairo text measurement" ON)

# Debug/sanitizer/coverage options
option(PREFIGURE_SANITIZE "Enable AddressSanitizer + UBSan" OFF)
option(PREFIGURE_COVERAGE "Enable code coverage (gcov)" OFF)

if(PREFIGURE_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizers enabled: AddressSanitizer + UndefinedBehaviorSanitizer")
endif()

if(PREFIGURE_COVERAGE)
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
    message(STATUS "Code coverage enabled (gcov)")
endif()
option(PREFIGURE_WITH_LIBLOUIS "Braille translation via liblouis" ON)

include(FetchContent)

# ---- Required dependencies ----

# Eigen3
find_package(Eigen3 3.4 QUIET)
if(NOT Eigen3_FOUND)
    message(STATUS "Eigen3 not found, fetching via FetchContent...")
    FetchContent_Declare(
        eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG 3.4.0
        GIT_SHALLOW TRUE
    )
    set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(EIGEN_BUILD_PKGCONFIG OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(eigen)
endif()

# spdlog
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    message(STATUS "spdlog not found, fetching via FetchContent...")
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# pugixml (vendored)
add_library(pugixml STATIC
    third_party/pugixml/pugixml.cpp
)
target_include_directories(pugixml PUBLIC third_party/pugixml)

# cpp-base64 (vendored)
add_library(cpp_base64 STATIC
    third_party/cpp-base64/base64.cpp
)
target_include_directories(cpp_base64 PUBLIC third_party/cpp-base64)

# exprtk is header-only (vendored in third_party/exprtk/)
# rapidcsv is header-only (vendored in third_party/rapidcsv/)

# ---- Optional dependencies ----

if(PREFIGURE_WITH_DIFFEQS OR PREFIGURE_WITH_NETWORK)
    find_package(Boost 1.74 QUIET)
    if(NOT Boost_FOUND)
        message(STATUS "Boost not found, fetching headers via FetchContent...")
        FetchContent_Declare(
            boost
            URL https://github.com/boostorg/boost/releases/download/boost-1.85.0/boost-1.85.0-cmake.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        )
        set(BOOST_INCLUDE_LIBRARIES graph CACHE STRING "" FORCE)
        FetchContent_MakeAvailable(boost)
    endif()
endif()

if(PREFIGURE_WITH_SHAPES)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GEOS geos)
    endif()
    if(NOT GEOS_FOUND)
        find_library(GEOS_LIBRARY NAMES geos_c)
        find_path(GEOS_INCLUDE_DIR geos_c.h)
        if(GEOS_LIBRARY AND GEOS_INCLUDE_DIR)
            set(GEOS_FOUND TRUE)
        else()
            message(WARNING "GEOS not found, disabling shape support")
            set(PREFIGURE_WITH_SHAPES OFF)
        endif()
    endif()
endif()

if(PREFIGURE_WITH_CAIRO)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CAIRO cairo)
    endif()
    if(NOT CAIRO_FOUND)
        find_library(CAIRO_LIBRARY NAMES cairo)
        find_path(CAIRO_INCLUDE_DIR cairo.h PATH_SUFFIXES cairo)
        if(CAIRO_LIBRARY AND CAIRO_INCLUDE_DIR)
            set(CAIRO_FOUND TRUE)
            set(CAIRO_LIBRARIES ${CAIRO_LIBRARY})
            set(CAIRO_INCLUDE_DIRS ${CAIRO_INCLUDE_DIR})
        else()
            message(WARNING "Cairo not found, disabling Cairo text measurement")
            set(PREFIGURE_WITH_CAIRO OFF)
        endif()
    endif()
endif()

if(PREFIGURE_WITH_LIBLOUIS)
    find_library(LIBLOUIS_LIBRARY NAMES louis)
    find_path(LIBLOUIS_INCLUDE_DIR liblouis/liblouis.h)
    if(LIBLOUIS_LIBRARY AND LIBLOUIS_INCLUDE_DIR)
        set(LIBLOUIS_FOUND TRUE)
    else()
        message(WARNING "liblouis not found, disabling braille translation")
        set(PREFIGURE_WITH_LIBLOUIS OFF)
    endif()
endif()

# ---- Core library ----

set(PREFIGURE_CORE_SOURCES
    src/calculus.cpp
    src/math_utilities.cpp
    src/ctm.cpp
    src/utilities.cpp
    src/user_namespace.cpp
    src/definition.cpp
    src/diagram.cpp
    src/parse.cpp
    src/tags.cpp
    src/coordinates.cpp
    src/annotations.cpp
    src/label.cpp
    src/label_tools.cpp
    src/legend.cpp
    src/arrow.cpp
    src/clip.cpp
    src/group.cpp
    src/repeat.cpp
    src/read.cpp
    src/image.cpp
    src/graph.cpp
    src/area.cpp
    src/circle.cpp
    src/line.cpp
    src/point.cpp
    src/polygon.cpp
    src/rectangle.cpp
    src/path_element.cpp
    src/vector_element.cpp
    src/parametric_curve.cpp
    src/tangent_line.cpp
    src/implicit.cpp
    src/riemann_sum.cpp
    src/slope_field.cpp
    src/statistics.cpp
    src/axes.cpp
    src/grid_axes.cpp
)

# Conditional sources
if(PREFIGURE_WITH_DIFFEQS)
    list(APPEND PREFIGURE_CORE_SOURCES src/diffeqs.cpp)
endif()
if(PREFIGURE_WITH_NETWORK)
    list(APPEND PREFIGURE_CORE_SOURCES src/network.cpp)
endif()
if(PREFIGURE_WITH_SHAPES)
    list(APPEND PREFIGURE_CORE_SOURCES src/shape.cpp)
endif()

add_library(prefigure STATIC ${PREFIGURE_CORE_SOURCES})

target_include_directories(prefigure PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/exprtk
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rapidcsv
)

target_link_libraries(prefigure PUBLIC
    Eigen3::Eigen
    spdlog::spdlog
    pugixml
    cpp_base64
)

# Feature compile definitions
if(PREFIGURE_WITH_DIFFEQS)
    target_compile_definitions(prefigure PUBLIC PREFIGURE_HAS_DIFFEQS)
    if(TARGET Boost::headers)
        target_link_libraries(prefigure PUBLIC Boost::headers)
    elseif(Boost_FOUND)
        target_include_directories(prefigure PUBLIC ${Boost_INCLUDE_DIRS})
    endif()
    # ODE integration is implemented directly (Dormand-Prince RK45) to avoid
    # pulling in the transitive dependency tree of Boost.Odeint.
endif()

if(PREFIGURE_WITH_NETWORK)
    target_compile_definitions(prefigure PUBLIC PREFIGURE_HAS_NETWORK)
    if(TARGET Boost::graph)
        target_link_libraries(prefigure PUBLIC Boost::graph)
    elseif(TARGET Boost::headers)
        target_link_libraries(prefigure PUBLIC Boost::headers)
    elseif(Boost_FOUND)
        target_include_directories(prefigure PUBLIC ${Boost_INCLUDE_DIRS})
    endif()
endif()

if(PREFIGURE_WITH_SHAPES)
    target_compile_definitions(prefigure PUBLIC PREFIGURE_HAS_SHAPES)
    if(GEOS_INCLUDE_DIRS)
        target_include_directories(prefigure PRIVATE ${GEOS_INCLUDE_DIRS})
    endif()
    if(GEOS_LIBRARIES)
        target_link_libraries(prefigure PRIVATE ${GEOS_LIBRARIES})
    elseif(GEOS_LIBRARY)
        target_link_libraries(prefigure PRIVATE ${GEOS_LIBRARY})
    endif()
endif()

if(PREFIGURE_WITH_CAIRO)
    target_compile_definitions(prefigure PUBLIC PREFIGURE_HAS_CAIRO)
    target_include_directories(prefigure PRIVATE ${CAIRO_INCLUDE_DIRS})
    target_link_libraries(prefigure PRIVATE ${CAIRO_LIBRARIES})
endif()

if(PREFIGURE_WITH_LIBLOUIS)
    target_compile_definitions(prefigure PUBLIC PREFIGURE_HAS_LIBLOUIS)
    target_include_directories(prefigure PRIVATE ${LIBLOUIS_INCLUDE_DIR})
    target_link_libraries(prefigure PRIVATE ${LIBLOUIS_LIBRARY})
endif()

# ---- Python bindings ----

if(PREFIGURE_BUILD_PYTHON)
    find_package(pybind11 QUIET)
    if(NOT pybind11_FOUND)
        FetchContent_Declare(
            pybind11
            GIT_REPOSITORY https://github.com/pybind/pybind11.git
            GIT_TAG v2.13.6
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(pybind11)
    endif()

    pybind11_add_module(_prefigure
        bindings/module.cpp
        bindings/py_parse.cpp
        bindings/py_diagram.cpp
        bindings/py_types.cpp
    )
    target_link_libraries(_prefigure PRIVATE prefigure)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        install(TARGETS _prefigure DESTINATION prefig)
    endif()
endif()

# ---- Tests ----

if(PREFIGURE_BUILD_TESTS)
    enable_testing()
    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG v3.7.1
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    add_subdirectory(tests)
endif()

# ---- Summary ----

message(STATUS "")
message(STATUS "PreFigure C++ configuration:")
message(STATUS "  C++ standard:    C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Build type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  Python bindings: ${PREFIGURE_BUILD_PYTHON}")
message(STATUS "  Tests:           ${PREFIGURE_BUILD_TESTS}")
message(STATUS "  ODE solver:      ${PREFIGURE_WITH_DIFFEQS}")
message(STATUS "  Network graphs:  ${PREFIGURE_WITH_NETWORK}")
message(STATUS "  Shape geometry:  ${PREFIGURE_WITH_SHAPES}")
message(STATUS "  Cairo text:      ${PREFIGURE_WITH_CAIRO}")
message(STATUS "  Braille:         ${PREFIGURE_WITH_LIBLOUIS}")
message(STATUS "")
