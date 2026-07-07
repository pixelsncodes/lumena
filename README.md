# LumenMelody

Core C++ library that turns an image into a MIDI melody. Built as a static
library (`LumenMelody`) intended to be embedded into a JUCE-based synth plugin.

> Status: **skeleton**. The build system, module structure, dependency wiring
> and a test runner are in place. Feature logic is not implemented yet — each
> module exposes its intended interface with `// TODO` markers.

## Pipeline

The generator is organised as four stages, one per source subdirectory:

| Stage       | Directory     | Responsibility                                   |
|-------------|---------------|--------------------------------------------------|
| Image       | `src/image/`  | Overlay a grid on an image, sample cell brightness |
| Scales      | `src/scales/` | Manage musical scales, loaded from JSON config   |
| Markov      | `src/markov/` | Note-to-note transitions via a Markov chain      |
| MIDI        | `src/midi/`   | Assemble generated notes into a MIDI sequence     |

`config/` holds the JSON configuration (`scales.json`, `settings.json`).
`tests/` holds the unit-test runner.

## Requirements

- CMake ≥ 3.16
- A C++17 compiler
- Network access on first configure (to fetch nlohmann/json), or a
  system-installed `nlohmann_json` package

[nlohmann/json](https://github.com/nlohmann/json) is pulled in automatically via
CMake `FetchContent` (falling back to `find_package` if already installed).

## Build & test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Or run the test binary directly:

```sh
./build/bin/LumenMelodyTests
```

## Embedding in the JUCE plugin

Add this repository as a subdirectory of the plugin's CMake build and link the
target:

```cmake
add_subdirectory(external/LumenMelody)
target_link_libraries(MyPlugin PRIVATE LumenMelody::LumenMelody)
```

When used as a subproject, the tests are disabled by default
(`LUMENMELODY_BUILD_TESTS=OFF`).
