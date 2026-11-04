cmake_minimum_required(VERSION 3.22)

# JUCE requires the C language to be enabled in addition to C++.
project(ChordSketch VERSION 0.1.0 LANGUAGES C CXX)

# Link the MSVC runtime statically so the VST3 does not depend on the VC++ redistributable being
# installed on the user's machine. Must match across chordcore, Catch2 and the plugin.
option(HC_STATIC_MSVC_RUNTIME "Use the static MSVC runtime (/MT)" ON)
if(MSVC AND HC_STATIC_MSVC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HC_BUILD_TESTS "Build the chordcore unit tests (fetches Catch2)" ON)
option(HC_BUILD_TOOLS "Build command-line tools (export_demo)" ON)
option(HC_BUILD_PLUGIN "Build the JUCE VST3 plugin (fetches JUCE)" ON)
option(HC_WARNINGS_AS_ERRORS "Treat chordcore compiler warnings as errors" OFF)

add_subdirectory(core)

if(HC_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(HC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(HC_BUILD_PLUGIN)
    add_subdirectory(plugin)
endif()
