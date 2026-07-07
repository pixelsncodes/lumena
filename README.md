# Lumena

Core C++ library that turns an image into a MIDI melody. Built as a static
library (`Lumena`) intended to be embedded into a JUCE-based synth plugin, but
fully usable on its own — the standalone build ships a command-line demo that
generates a `.mid` file from any image.

> Status: **standalone library complete**. The full pipeline — image analysis,
> key selection, melody generation and MIDI export — is implemented, tested and
> driveable end to end via the `lumena_demo` executable. No external runtime
> dependencies beyond a JSON parser used only for loading config.

## Features

- **Image analysis** (`src/image/`) — load PNG/JPEG via vendored `stb_image`,
  reduce the frame to a normalised brightness grid, and summarise average
  hue/saturation with a circular hue mean.
- **Key selection** (`src/scales/`) — map the image's average hue onto the
  circle of fifths to pick a key, and its saturation to major vs. relative
  minor pentatonic. All 24 keys are available; extra named scales load from
  `config/scales.json`.
- **Melody generation** (`src/markov/`, `src/melody/`) — a music-theory-weighted,
  first-order Markov chain over scale degrees, with dynamic voice-leading rules
  (leap resolution, third-repeat damping) and an image-brightness bias hook. On
  top of the walk, a **phrase layer** (the default `Phrased` mode) shapes the
  output into a motif (A), a transposed variation (A′), a contrasting phrase (B)
  and a tonic cadence — extended as `A A′ B A″ …` for longer sequences — with
  probabilistic rests between phrases, tonic/fifth-leaning phrase endings and
  occasional arpeggio ornaments. The original flat walk remains available as
  `Freeform` mode. Fully reproducible from a fixed RNG seed.
- **MIDI export** (`src/midi/`) — a **dependency-free** Standard MIDI File
  writer. Converts beat-timed notes to a tick-based event stream (correct
  note-off-before-note-on ordering at equal ticks) and serialises format-0 SMF
  bytes with hand-rolled variable-length-quantity delta times. Write to a file
  or to an in-memory `std::vector<uint8_t>` for host-side drag-and-drop export.

## Pipeline

The generator is organised as four stages, one per source subdirectory:

| Stage  | Directory     | Responsibility                                     |
|--------|---------------|----------------------------------------------------|
| Image  | `src/image/`  | Overlay a grid on an image, sample cell brightness |
| Scales | `src/scales/` | Manage musical scales; pick a key from image colour |
| Markov | `src/markov/` | Note-to-note transitions via a Markov chain        |
| MIDI   | `src/midi/`   | Assemble generated notes into a Standard MIDI File  |

`config/` holds the JSON configuration (`scales.json`, `settings.json`).
`apps/` holds the demo driver and `tests/` the unit-test runner.

## Requirements

- CMake ≥ 3.16
- A C++17 compiler
- Network access on first configure (to fetch nlohmann/json), or a
  system-installed `nlohmann_json` package

[nlohmann/json](https://github.com/nlohmann/json) is pulled in automatically via
CMake `FetchContent` (falling back to `find_package` if already installed). It
is used only for reading config; the MIDI writer has no third-party dependency.

## Build & test

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Or run the test binary directly:

```sh
./build/bin/LumenaTests
```

## Demo: image → MIDI

The standalone build produces `lumena_demo`, which runs the whole pipeline and
optionally writes a MIDI file:

```sh
./build/bin/lumena_demo path/to/image.png --out melody.mid
# optional: --seed N  (reproducible melody)  --tempo BPM  (default 120)
#           --rhythm straight|flowing  (default flowing: brightness-shaped
#                                       eighth/quarter/half notes)
#           --length N   (number of notes; default one per grid cell. In
#                         phrased mode this is an approximate target — whole
#                         phrases plus a closing cadence may run slightly over)
#           --cells walk|random  (default walk: the melody wanders the image
#                                 for a smooth line; random teleports per note)
#           --mode phrased|freeform  (default phrased: motif/variation/contrast/
#                                     cadence phrases with rests and ornaments;
#                                     freeform is the plain flat walk)
#           --arp 0..1   (phrased-mode arpeggio-ornament probability per phrase;
#                         default 0.15, prefers cells brighter than 0.7)
```

```
Loaded path/to/image.png (128x128)
Detected key: G Major Pentatonic (hue 37°, saturation 0.74)
Generated 34 notes at 120 BPM (phrased mode, flowing rhythm, walk cells)
Wrote MIDI file: melody.mid (64 events)
```

The resulting `melody.mid` opens in any DAW or MIDI player.

## Using the MIDI writer directly

```cpp
#include "midi/MidiSequence.h"
#include "midi/MidiFileWriter.h"

using namespace lumena::midi;

// Beat-timed notes from the generator (pitch, velocity, start, length).
std::vector<Note> notes = {
    {60, 100, 0.0, 1.0},  // middle C, one beat
    {64,  90, 1.0, 1.0},  // E, next beat
    {67,  90, 2.0, 2.0},  // G, held two beats
};

// Resolve to a tick timeline (120 BPM, 480 PPQ by default).
MidiSequence sequence(notes, /*tempoBpm=*/120.0, /*ppq=*/480);

// Write a Standard MIDI File...
MidiFileWriter::write(sequence, "out.mid");

// ...or get the bytes in memory (e.g. for drag-and-drop export).
std::vector<std::uint8_t> bytes = MidiFileWriter::toBytes(sequence);
```

## Embedding in the JUCE plugin

Add this repository as a subdirectory of the plugin's CMake build and link the
target:

```cmake
add_subdirectory(external/lumena)
target_link_libraries(MyPlugin PRIVATE Lumena::Lumena)
```

When used as a subproject, the demo and tests are disabled by default
(`LUMENA_BUILD_TESTS=OFF`, and `lumena_demo` builds only in standalone mode).
