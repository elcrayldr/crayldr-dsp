cmake_minimum_required(VERSION 3.22)
project(CrayLdrVoxLeveler VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CRAYLDR_BUILD_PLUGIN "Build the JUCE VST3 plugin (downloads JUCE)" ON)
option(CRAYLDR_BUILD_TESTS  "Build and register the core test suite"      ON)
option(CRAYLDR_SANITIZE     "ASan+UBSan for the tests (GCC/Clang only)"     OFF)
set(CRAYLDR_JUCE_TAG "8.0.6" CACHE STRING "JUCE git tag to fetch")

if(MSVC)
  add_compile_options(/utf-8)
endif()

# ---------------- JUCE-independent DSP core ----------------
add_library(VoxLevelerCore STATIC
  Source/Core/CurveBuilder.cpp
  Source/Core/Serialization.cpp
  Source/Core/VoxLevelerCore.cpp)
target_include_directories(VoxLevelerCore PUBLIC Source)
target_compile_features(VoxLevelerCore PUBLIC cxx_std_17)
set_target_properties(VoxLevelerCore PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(VoxLevelerCore PRIVATE /W4)
else()
  target_compile_options(VoxLevelerCore PRIVATE -Wall -Wextra)
endif()

# ---------------- tests ----------------
if(CRAYLDR_BUILD_TESTS)
  enable_testing()
  add_executable(VoxLevelerTests Tests/CoreTests.cpp)
  target_link_libraries(VoxLevelerTests PRIVATE VoxLevelerCore)
  find_package(Threads REQUIRED)
  target_link_libraries(VoxLevelerTests PRIVATE Threads::Threads)
  target_compile_definitions(VoxLevelerTests PRIVATE _USE_MATH_DEFINES)
  if(NOT MSVC)
    target_compile_options(VoxLevelerTests PRIVATE -Wno-mismatched-new-delete)
    if(CRAYLDR_SANITIZE)
      target_compile_options(VoxLevelerTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(VoxLevelerTests PRIVATE -fsanitize=address,undefined)
      target_compile_options(VoxLevelerCore PRIVATE -fsanitize=address,undefined)
    endif()
  endif()
  add_test(NAME CoreTests COMMAND VoxLevelerTests)
  # The harness must be able to fail even in Release/NDEBUG builds:
  add_test(NAME NegativeControl COMMAND VoxLevelerTests --negative-control)
  set_tests_properties(NegativeControl PROPERTIES WILL_FAIL TRUE)
endif()

# ---------------- VST3 plugin ----------------
if(CRAYLDR_BUILD_PLUGIN)
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${CRAYLDR_JUCE_TAG}
    GIT_SHALLOW    ON)
  FetchContent_MakeAvailable(JUCE)

  juce_add_plugin(CrayLdrVoxLeveler
    COMPANY_NAME "Cray LDR"
    PRODUCT_NAME "CRAY LDR VOX LEVELER"
    PLUGIN_MANUFACTURER_CODE CrLd
    PLUGIN_CODE VxLv
    FORMATS VST3
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

  target_sources(CrayLdrVoxLeveler PRIVATE
    Source/PluginProcessor.cpp Source/PluginProcessor.h
    Source/PluginEditor.cpp Source/PluginEditor.h)

  target_compile_definitions(CrayLdrVoxLeveler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

  target_link_libraries(CrayLdrVoxLeveler
    PRIVATE
      VoxLevelerCore
      juce::juce_audio_utils
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_lto_flags
      juce::juce_recommended_warning_flags)
endif()
