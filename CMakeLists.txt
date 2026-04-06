cmake_minimum_required(VERSION 3.24)
project(prefig_python VERSION 0.6.0 LANGUAGES CXX)

add_subdirectory(prefigure-cpp)

install(TARGETS _prefigure
    DESTINATION prefig
    COMPONENT python)
