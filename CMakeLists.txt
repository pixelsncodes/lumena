cmake_minimum_required(VERSION 3.16)

project(LumenMelody
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
    set(LUMENMELODY_STANDALONE ON)
else()
    set(LUMENMELODY_STANDALONE OFF)
endif()

if(LUMENMELODY_STANDALONE AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(LUMENMELODY_BUILD_TESTS "Build the LumenMelody unit tests" ${LUMENMELODY_STANDALONE})

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
add_library(LumenMelody STATIC
    src/LumenMelody.cpp
    src/image/ImageGrid.cpp
    src/image/BrightnessSampler.cpp
    src/scales/Scale.cpp
    src/scales/ScaleManager.cpp
    src/markov/MarkovChain.cpp
    src/midi/MidiSequence.cpp)

add_library(LumenMelody::LumenMelody ALIAS LumenMelody)

target_include_directories(LumenMelody
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(LumenMelody
    PUBLIC
        nlohmann_json::nlohmann_json)

target_compile_features(LumenMelody PUBLIC cxx_std_17)

target_compile_definitions(LumenMelody
    PUBLIC
        LUMENMELODY_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(LumenMelody PRIVATE /W4)
else()
    target_compile_options(LumenMelody PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
if(LUMENMELODY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
