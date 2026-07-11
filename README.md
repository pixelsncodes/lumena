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

## How the melody engine works: the math

The whole pipeline is a pure function of three inputs — the **image bytes**, the
**generation settings**, and a single **RNG seed**. Fix all three and the emitted
MIDI is byte-identical, on any machine. Everything below is derived from that one
mt19937 stream and a stack of deterministic maps; the only randomness is the
seed. Formulas cite the file and function they live in.

### 1. Image features

The image is first reduced to a **brightness grid**: the frame is partitioned
into `columns × rows` cells with no gaps or overlap, and each cell's value is the
mean Rec. 709 perceived luminance of its pixels (`image/Luma.h · luma709`,
`image/BrightnessGrid.cpp · cellSpan`):

```
L = 0.2126·R + 0.7152·G + 0.0722·B          (0..255)
```

Cell values are normalised to `[0, 1]` one of two ways (`BrightnessGrid.cpp`
constructor): **Absolute** divides by 255; **Stretched** min–max-normalises the
grid, `(L − Lmin) / (Lmax − Lmin)`, and flattens to 0 when the grid has no
contrast. This normalised brightness `b` is the single most-used feature — it
drives velocity, duration, register target and the walk.

Colour is summarised over every pixel as a **saturation-weighted circular hue
mean**, plus mean saturation and mean luma (`image/ColorAnalysis.cpp ·
averageHueSaturation`, per-pixel `rgbToHsv`):

```
hue        = atan2( Σ s·sinθ , Σ s·cosθ )     (θ = pixel hue, wrapped to [0,360))
saturation = mean(s)
value      = mean(L / 255)
```

Weighting each hue vector by its saturation `s` means greys barely vote, so the
mean hue tracks the image's real colour rather than washing to noise.

Local **detail / edges** is the brightness range over a cell's 8-connected
neighbourhood (wrapped edges), `contrast = max − min` (`MelodyGenerator.cpp ·
localContrast`). Its mean over the grid, `imageDetail ∈ [0, 1]`
(`computeImageDetail`), is a cheap "how busy is this image" scalar.

### 2. Key and scale from colour

Hue maps onto the **circle of fifths** and colour statistics choose the scale
type (`scales/KeySelector.cpp`):

```
position  = round(hue / 30) mod 12            (0=C, 1=G, 2=D, … clockwise)
rootNote  = 60 + (position · 7 mod 12)        (tonic pitch class in octave 4)
```

`chooseScaleType(saturation, value)` is a **tuned decision tree**, not a theory
(thresholds are audition constants):

- saturation `< 0.05` — hue unreliable, fall back to A minor pentatonic.
- saturation `< 0.22` (washed out) — pentatonic; major if `value ≥ 0.5`, else minor.
- saturation `≥ 0.70` (vivid) and dark — harmonic minor if `value < 0.18`,
  blues minor if `value < 0.42`.
- otherwise a diatonic mode picked along the dark→bright axis:
  `idx = clamp(⌊value · 6⌋, 0, 5)` over
  `[Phrygian, Aeolian, Dorian, Mixolydian, Ionian, Lydian]`.

### 3. Feature → musical-parameter maps

Each mapping is a plain closed-form function of `b` (brightness) or a derived
feature. Ranges and weights are tuned constants, called out where they are.

| Musical parameter | Map | Source |
|---|---|---|
| **Velocity** | `50 + round(b · 65)` → `[50, 115]`, then Energy-scaled `×(0.7 + 0.6·energy)` | `brightnessToVelocity`, `applyEnergy` |
| **Register / contour target** | `degree = clamp(⌊b · totalDegrees⌋, 0, N−1)` | `scales/ScaleLibrary.cpp · mapBrightnessToDegree` |
| **Note length** (Flowing) | pick `{0.5, 1.0, 2.0}` beats with weights `{0.15+b, 0.60, 0.15+(1−b)}` — dark leans long, bright leans short | `flowingDuration` |
| **Rhythmic density** | split a note into `1 + clamp(round(contrast · amount · 3), 0, 3)` equal pieces | `densitySubdivisionsWanted` |
| **Groove choice** | `activity = clamp(0.5·energy + 0.5·imageDetail)`; template weight `wᵢ = 0.04 + matchᵢ³`, `matchᵢ = 1 − |activity − i/(N−1)|` | `pickRhythmTemplate` |
| **Phrase contour** | central differences `sₓ, s_y` around the start cell; `slope = sₓ + s_y`; `> 0.03` Rise, `< −0.03` Fall, else Arch | `selectContour` |
| **Motif variation** | transpose `Δ = clamp(imageDeg − anchor, −2, +2)` scale degrees (then reined to ±1) | `varyMotif` |

`brightnessBias` (surfaced as **Image Influence**) is the knob that decides how
literally these image targets are followed versus the Markov chain's own
preference — see §4.

### 4. The Markov chain (pitch)

Pitch is a first-order, music-theory-weighted Markov walk over scale degrees. The
base transition matrix `P(i→j)` is built once from theory
(`markov/TransitionMatrix.cpp · fromTheory`):

```
interval  = (dist==0) ? repeatWeight : intervalDecay^(dist−1),   dist = |i−j|
gravity   = 1 + tonicGravity·tonicProximity(j) + centerGravity·centerProximity(j)
P(i→j)    ∝ max(0, interval · gravity)            (rows normalised to sum 1)

tonicProximity(j)  = 1 / (1 + distance to nearest tonic degree)
centerProximity(j) = max(0, 1 − |j − center| / center),   center = (N−1)/2
```

so stepwise motion is likeliest (geometric interval decay), leaps get rarer, and
degrees drift gently toward the tonic and the middle of the range instead of
stranding at the extremes. Defaults (`markov/TheoryWeights.h`): `intervalDecay
0.5`, `repeatWeight 0.5`, `thirdRepeatDamping 0.25`, `tonicGravity 0.4`,
`centerGravity 0.3`, `leapThreshold 2`, `leapResolution 4`.

Two **second-order** adjustments rebuild the current row each step
(`MelodyChain.cpp · buildDynamicRow`): after a leap wider than `leapThreshold`,
the opposite-direction step is boosted `×(1 + leapResolution)` (leap
resolution); a third identical degree in a row is damped `×thirdRepeatDamping`.

The image steers the walk by blending a delta at the brightness-suggested target
into the (already normalised) row (`MelodyChain.cpp · nextBiased`):

```
p = (1 − b)·markovRow + b·δ(target),   b = brightnessBias
```

The chosen degree becomes a MIDI note via the scale
(`scales/Scale.cpp · noteAt`): `rootNote + intervals[degree mod dpo] + 12·⌊degree
/ dpo⌋`, where `dpo` is degrees-per-octave.

### 5. The harmonic frame

One **pop progression** is chosen for the whole session — a single RNG draw over
six four-chord templates (`I-V-vi-IV`, `vi-IV-I-V`, `I-vi-IV-V`, `I-IV-vi-V`,
`I-IV-V-vi`, `vi-IV-V-I`), stored as diatonic root degrees `{0=I, 3=IV, 4=V,
5=vi}` (`pickProgressionBase`, `progressionTemplates`). Supplying an explicit
progression (**Lock Harmony**) draws no RNG at all. The base is tiled one chord
per bar.

Each bar's chord is spelled as a real triad in the key's parent major/minor
(`PhraseBuilder::updateHarmonyTarget`):

```
bar   = tick / ticksPerBar
root  = progression[ bar mod n ]
pcₖ   = (tonicPc + steps[(root + 2k) mod 7]) mod 12,   k = 0,1,2
steps = major {0,2,4,5,7,9,11}  or  minor {0,2,3,5,7,8,10}
```

On **strong beats** the walked pitch prefers a chord tone
(`walkDegreesAt`): a beat is strong when `tick mod 960 == 0` **or** it is the
phrase's entry note (the 4.5-d entry accent), and on a strong beat an
unconditional coin `< 0.6` snaps the drawn degree to the nearest chord tone.

### 6. Phrase form (A / A′ / B / cadence)

`generatePhrased` assembles motifs. A motif is `motifLen ∈ [3, 5]` notes (one RNG
draw). Phrase 0 is a walked motif **A**; the body then alternates copied
A-family slots at odd positions (a verbatim repeat with probability `repetition`,
else a varied `varyMotif` transposition) with freshly walked, teleported **B**
phrases at even positions, always producing at least `A, A′, B` before the close:

```
while (phraseTotal < 3  ||  bodyNotes < target − motifLen):
    odd position  → copied A-family (A′, A″, …)
    even position → walked B
```

then a **closing phrase** that cadences onto the tonic. `varyMotif` preserves
interval content (so A′ stays recognisable): its transpose is reined to ±1 degree
and it may lift the whole repeat up an octave with probability `0.25`, but never
two lifts running.

Cadence rules: non-closing phrase endings lean to the nearest tonic/fifth with
bias `0.85` (`kPhraseEndBias`) and settle to at least a dotted quarter (`1.5`
beats); the closing note lands squarely on the tonic, approached by a single step
(the leading tone when the scale spells one), held for `2` or `4` beats — a coin
(`drawCadenceLength`). Strict monophony throughout.

Two rules give **B** its identity:

- **Related-region window (C-1).** B teleports to a random cell (two RNG draws),
  but its *pitch material* is read from a shadow cell remapped into a window of
  half-width `W = max(1, 2·columns / 16)` around motif A's anchor cell
  (`planPhraseCells`, `stepCell`). B thus draws melodic content from a region the
  image itself relates to A's, while the walk path stays on the drawn cells — the
  remap is post-draw, so it never perturbs the stream (see §8).
- **Open cadence (C-3).** B's last note snaps to the nearest degree with
  half-cadence function — the 2nd (pitch class 2) or 5th (7), tie-break to the
  fifth (`openBPhraseCadence`) — so B opens tension the following A-family return
  answers, instead of resolving like every other phrase.

### 7. The clock: plan-then-walk on a 960-tick grid

Time lives on an integer tick grid of **960 ticks per quarter-note**
(`kTicksPerBeat`). 960 = 2⁶·3·5 divides every subdivision the generator emits —
dyadic values (½ → 480), triplet ornaments (⅓ → 320) and density splits down to
1/64 of a beat (15 ticks) — so every onset and length is an exact integer tick
and the strong-beat test is exact integer arithmetic, not a float compare.

Each phrase runs **plan-then-walk** (`generatePhrased`): (1) the inter-phrase
rest decision, (2) the structural draws (repeat/vary, or B's teleport), (3) the
ornament plan's draws, (4) a fully deterministic **timing plan** that fixes every
emitted slot's real start tick from the cells alone, (5) the **pitch** walk
against those real ticks, (6) dynamics, (7) emission. Every draw that shapes
timing is consumed *before any pitch exists*, which is what makes the
pitch-never-touches-timing invariant structural rather than incidental.

Durations come from a session-locked two-bar groove tiled from tick 0
(`barAlignedDuration`): with the template's period in ticks, a note starting at
`tick` sounds until the next slot boundary, `(acc − (tick mod period)) / 960`
beats. A mid-slot start whose remainder is shorter than an eighth becomes an
**honest tied anticipation** — it sustains through the boundary to the end of the
next slot as one MIDI event (and is never density-split), so notes can legitimately
tie across bar lines (`planTiming`).

**Register continuity** (rules 4.5-d/4.5-e) keeps adjacent bars in the same
register, pitch-only and draw-free, in three cooperating parts:

1. a walked phrase's degrees are clamped within `±4` scale degrees
   (`kPhraseCompassDegrees`) of the phrase's entry note (`walkDegreesAt`), so one
   phrase can't straddle two registers;
2. at emission a whole phrase is octave-folded toward the previous emitted bar's
   pitch centroid when its own centroid strays past a band — `6` semitones for
   A-family/closing phrases, `9` for B phrases, which breathe wider on purpose
   (`applyRegisterContinuity`, `kRegisterBandA/B`);
3. a final per-bar rescue pass folds any bar still out of band by the nearest
   whole octaves that fit the scale, keeping one degree of edge headroom
   (`foldGroup`, `kFoldEdgeMargin`).

Octave folds preserve pitch classes and interval content, so chord snaps, B's
open cadence and the closing tonic all keep their function.

### 8. Determinism and the pitch-timing firewall

The `std::mt19937` is **borrowed, never seeded internally**
(`generateMelody`), so the caller's seed alone determines the stream: same image
+ same settings + same seed = byte-identical MIDI. A hard invariant is that
**pitch never influences the RNG draw stream** — every place the image or a
melodic choice could bend the result does so through a *post-draw* clamp or
remap of an already-sampled value (the compass clamp, the register folds, the
motif-transpose rein, and the C-1 window are all of this form), so the count and
order of draws are a pure function of the timing/structure domain. This is why B
can read a related image region without shifting a single onset, and it is pinned
permanently by `tests/MelodyGeneratorTests.cpp ·
test_pitch_domain_never_shifts_timing`.

Serialisation is a dependency-free Standard MIDI File writer
(`midi/MidiSequence.cpp`): beat-timed notes resolve to a tick timeline at the
file's PPQ (SMF default 480; the generator plans on the finer 960 grid), and
events are stably sorted so note-offs precede note-ons at an equal tick — a
deterministic, well-formed event stream.

> Historical note: before Phase 4.5 the engine ran two clocks (a provisional
> generation-time clock reconciled by a second pass), which coupled some pitch
> changes into rhythm on a minority of seeds. The single-tick plan-then-walk
> clock above retired that mechanism; it is documented only in
> `CLOCK_TRACE.md` / `PHASE45_REPORT.md` for reading old branches.

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
