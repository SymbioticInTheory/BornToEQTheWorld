cmake_minimum_required(VERSION 3.22)
project(BornToEQTheWorld VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# DSP core is header-only (src/dsp) so it builds and tests without JUCE.
add_executable(render_impulse tests/render_impulse.cpp)

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME dsp_matches_autoeq
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/compare_with_autoeq.py
                   $<TARGET_FILE:render_impulse>
                   "${CMAKE_SOURCE_DIR}/presets/sample/Sennheiser HD 650 ParametricEQ.txt")
endif()
