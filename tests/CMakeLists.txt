add_executable(LumenMelodyTests main.cpp)

target_link_libraries(LumenMelodyTests
    PRIVATE
        LumenMelody::LumenMelody
        nlohmann_json::nlohmann_json)

# Absolute path to the bundled config dir so the test runner can load JSON
# regardless of the working directory it is invoked from.
target_compile_definitions(LumenMelodyTests
    PRIVATE
        LUMENMELODY_CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")

add_test(NAME LumenMelodyTests COMMAND LumenMelodyTests)
