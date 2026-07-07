cmake_minimum_required(VERSION 3.16)

project(Lumena
    VERSION 0.1.0
    DESCRIPTION "Image-driven melody generator core for a JUCE synth plugin"
    LANGUAGES CXX)

# --------------------------------------------------------------------------
# Global settings
# --------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put binaries in predictable locations for standalone builds.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Detect whether we are the top-level project (standalone) or a subproject
# pulled into a larger build such as the JUCE plugin.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(LUMENA_STANDALONE ON)
else()
    set(LUMENA_STANDALONE OFF)
endif()

if(LUMENA_STANDALONE AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LUMENA_BUILD_TESTS "Build the Lumena unit tests" ${LUMENA_STANDALONE})

# --------------------------------------------------------------------------
# Dependencies: nlohmann/json (via FetchContent)
# --------------------------------------------------------------------------
include(FetchContent)

# Prefer an already-installed copy if the host provides one; otherwise fetch.
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")

FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    FIND_PACKAGE_ARGS NAMES nlohmann_json)

FetchContent_MakeAvailable(nlohmann_json)

# --------------------------------------------------------------------------
# Library target
# --------------------------------------------------------------------------
add_library(Lumena STATIC
    src/Lumena.cpp
    src/image/Image.cpp
    src/image/BrightnessGrid.cpp
    src/image/ColorAnalysis.cpp
    src/scales/Scale.cpp
    src/scales/ScaleLibrary.cpp
    src/scales/KeySelector.cpp
    src/markov/TheoryWeights.cpp
    src/markov/TransitionMatrix.cpp
    src/markov/MelodyChain.cpp
    src/midi/MidiSequence.cpp
    src/midi/MidiFileWriter.cpp
    src/melody/MelodyGenerator.cpp)

add_library(Lumena::Lumena ALIAS Lumena)

target_include_directories(Lumena
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # Vendored single-header libraries (stb_image); implementation detail,
        # so kept off the public interface.
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

target_link_libraries(Lumena
    PUBLIC
        nlohmann_json::nlohmann_json)

target_compile_features(Lumena PUBLIC cxx_std_17)

target_compile_definitions(Lumena
    PUBLIC
        LUMENA_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(Lumena PRIVATE /W4)
else()
    target_compile_options(Lumena PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------------------------------
# Demo executable (standalone builds only)
# --------------------------------------------------------------------------
# A small command-line driver that runs the whole pipeline on an image and can
# write the result as a Standard MIDI File. Not built when Lumena is embedded as
# a subproject.
if(LUMENA_STANDALONE)
    add_executable(lumena_demo apps/lumena_demo.cpp)
    target_link_libraries(lumena_demo PRIVATE Lumena::Lumena)
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if(LUMENA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
