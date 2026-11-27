cmake_minimum_required(VERSION 3.22)
project(BornToEQTheWorld VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BTEQ_BUILD_TESTS "Build the DSP and library tests" ON)
option(BTEQ_BUILD_PLUGIN "Build the JUCE plugin (VST3 + standalone)" ON)

find_package(Python3 COMPONENTS Interpreter)

# ---------------------------------------------------------------------------
# Tests. The DSP core (src/dsp) is header-only and builds without JUCE.
# ---------------------------------------------------------------------------
if(BTEQ_BUILD_TESTS)
  enable_testing()
  add_executable(render_impulse tests/render_impulse.cpp)
  add_executable(render_graphic tests/render_graphic.cpp)
  add_executable(test_library tests/test_library.cpp)
  add_executable(test_apo_config tests/test_apo_config.cpp)
  add_test(NAME apo_config COMMAND test_apo_config)
  add_executable(test_image_theme tests/test_image_theme.cpp)
  add_test(NAME image_theme COMMAND test_image_theme)
  add_executable(test_spectrum tests/test_spectrum.cpp)
  add_test(NAME spectrum COMMAND test_spectrum)
  add_test(NAME headphone_library
           COMMAND test_library ${CMAKE_SOURCE_DIR}/data/headphones.tsv ${CMAKE_SOURCE_DIR}/presets/sample)
  if(Python3_FOUND)
    add_test(NAME dsp_matches_autoeq
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/compare_with_autoeq.py
                     $<TARGET_FILE:render_impulse>
                     "${CMAKE_SOURCE_DIR}/presets/sample/Sennheiser HD 650 ParametricEQ.txt")
    add_test(NAME graphic_eq_fir
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tests/compare_graphic.py
                     $<TARGET_FILE:render_graphic>
                     "${CMAKE_SOURCE_DIR}/presets/sample/Sennheiser HD 650 GraphicEQ.txt")
  endif()
endif()

# ---------------------------------------------------------------------------
# Plugin
# ---------------------------------------------------------------------------
if(BTEQ_BUILD_PLUGIN)
  if(NOT Python3_FOUND)
    message(FATAL_ERROR "Python 3 is needed to pack the headphone library into the plugin")
  endif()

  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(JUCE)

  # The library is embedded gzip-compressed (about 22 MB -> 5 MB).
  set(BTEQ_LIBRARY_GZ ${CMAKE_BINARY_DIR}/headphones.tsv.gz)
  add_custom_command(
    OUTPUT ${BTEQ_LIBRARY_GZ}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gzip_file.py
      ${CMAKE_SOURCE_DIR}/data/headphones.tsv ${BTEQ_LIBRARY_GZ}
    DEPENDS ${CMAKE_SOURCE_DIR}/data/headphones.tsv ${CMAKE_SOURCE_DIR}/tools/gzip_file.py
    COMMENT "Compressing headphone library")
  set(BTEQ_CURVES_GZ ${CMAKE_BINARY_DIR}/curves.tsv.gz)
  add_custom_command(
    OUTPUT ${BTEQ_CURVES_GZ}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/gzip_file.py
      ${CMAKE_SOURCE_DIR}/data/curves.tsv ${BTEQ_CURVES_GZ}
    DEPENDS ${CMAKE_SOURCE_DIR}/data/curves.tsv ${CMAKE_SOURCE_DIR}/tools/gzip_file.py
    COMMENT "Compressing measurement and target curves")
  add_custom_target(bteq_library_gz DEPENDS ${BTEQ_LIBRARY_GZ} ${BTEQ_CURVES_GZ})

  juce_add_binary_data(BteqData
    SOURCES
      ${BTEQ_LIBRARY_GZ}
      ${BTEQ_CURVES_GZ}
      resources/forest.png
      resources/backgrounds/sunset.jpg
      resources/backgrounds/ocean.jpg
      resources/backgrounds/neon.jpg
      resources/backgrounds/moss.jpg
      resources/backgrounds/concrete.jpg
      resources/lagrangian.png
      resources/fonts/PirataOne-Regular.ttf
      resources/fonts/BarlowCondensed-SemiBold.ttf
      resources/fonts/BarlowCondensed-Bold.ttf
      resources/fonts/Barlow-Regular.ttf
      resources/fonts/Barlow-SemiBold.ttf)
  add_dependencies(BteqData bteq_library_gz)

  juce_add_plugin(BornToEQTheWorld
    COMPANY_NAME "SymbioticInTheory"
    PRODUCT_NAME "Born To EQ The World"
    PLUGIN_MANUFACTURER_CODE Symb
    PLUGIN_CODE Bteq
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    ICON_BIG resources/icon.png
    VST3_CATEGORIES Fx EQ)

  target_sources(BornToEQTheWorld PRIVATE
    src/plugin/PluginProcessor.cpp
    src/plugin/PluginEditor.cpp
    src/plugin/AnalyserWindow.cpp
    src/plugin/ApoBridge.cpp
    src/plugin/StandaloneAudio.cpp
    src/plugin/Loopback.cpp)
  target_include_directories(BornToEQTheWorld PRIVATE src)

  target_compile_definitions(BornToEQTheWorld PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_ASIO=1)  # standalone only; JUCE ships the ASIO SDK (GPLv3)

  target_link_libraries(BornToEQTheWorld
    PRIVATE
      BteqData
      juce::juce_audio_utils
      juce::juce_dsp
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)
endif()
