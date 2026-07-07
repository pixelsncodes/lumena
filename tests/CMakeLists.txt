add_executable(LumenaTests
    main.cpp
    ImageTests.cpp
    ColorAnalysisTests.cpp
    ScaleTests.cpp
    KeySelectorTests.cpp
    MarkovTests.cpp
    MidiTests.cpp
    MelodyGeneratorTests.cpp)

target_link_libraries(LumenaTests
    PRIVATE
        Lumena::Lumena
        nlohmann_json::nlohmann_json)

# Absolute path to the bundled config dir so the test runner can load JSON
# regardless of the working directory it is invoked from.
target_compile_definitions(LumenaTests
    PRIVATE
        LUMENA_CONFIG_DIR="${CMAKE_SOURCE_DIR}/config"
        # A writable scratch directory for the MIDI file round-trip test.
        LUMENA_TEST_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}")

add_test(NAME LumenaTests COMMAND LumenaTests)
