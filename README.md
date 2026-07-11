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

This section shows, step by step, how a single still image becomes a short piece
of music that is exactly reproducible. Nothing is uploaded and nothing is random
beyond one seed: fix the image, the settings, and that seed, and the same MIDI
comes out every time, on any machine. Read it top to bottom — the image is
measured (**A**), a key is chosen (**B**), those measurements become musical
targets (**C**), a Markov walk picks the actual notes (**D**), a tick-accurate
clock places and shapes them (**E**), and a firewall guarantees the result is
deterministic (**F**). Every equation cites the file and function it lives in.

### A. Image Analysis

**1. Perceived luminance.** The frame is partitioned into `columns × rows` cells
with no gaps or overlap, and each cell's value is the mean Rec. 709 perceived
luminance of its pixels (`image/Luma.h · luma709`, `image/BrightnessGrid.cpp ·
cellSpan`):

$$L = 0.2126\,R + 0.7152\,G + 0.0722\,B \qquad (0 \le L \le 255)$$

**2. Brightness normalisation.** Cell values become the most-used feature `b ∈
[0,1]` one of two ways — **Absolute** divides by 255, **Stretched** min–max-normalises
the grid and flattens to 0 when the grid has no contrast (`BrightnessGrid.cpp`
constructor):

$$b_{\text{absolute}} = \frac{L}{255}, \qquad b_{\text{stretched}} = \frac{L - L_{\min}}{L_{\max} - L_{\min}}$$

**3. Saturation-weighted circular hue mean.** Colour is summarised over every
pixel, each hue vector weighted by its saturation `s` so greys barely vote and the
mean tracks the image's real colour (`image/ColorAnalysis.cpp · averageHueSaturation`,
per-pixel `rgbToHsv`):

$$\text{hue} = \operatorname{atan2}\!\Big(\sum_p s_p \sin\theta_p,\ \sum_p s_p \cos\theta_p\Big), \qquad \text{saturation} = \overline{s}, \qquad \text{value} = \overline{L/255}$$

**4. Local contrast and image detail.** Edge strength at a cell is the brightness
range over its 8-connected, wrapped-edge neighbourhood, and its grid mean is a
cheap "how busy is this image" scalar (`MelodyGenerator.cpp · localContrast`,
`computeImageDetail`):

$$\text{contrast} = \max_{\mathcal{N}} L - \min_{\mathcal{N}} L, \qquad \text{imageDetail} = \overline{\text{contrast}} \in [0, 1]$$

### B. Key & Scale

**1. Root from hue via the circle of fifths.** Hue steps around the circle of
fifths in 30° increments to fix the tonic pitch class in octave 4 (`scales/KeySelector.cpp`):

$$\text{position} = \operatorname{round}(\text{hue}/30) \bmod 12, \qquad \text{rootNote} = 60 + (7\cdot\text{position} \bmod 12)$$

**2. Scale-type decision tree.** `chooseScaleType(saturation, value)` is a tuned
heuristic, not a theory — its thresholds are audition constants: saturation `<
0.05` falls back to A-minor pentatonic; `< 0.22` (washed out) is pentatonic, major
if `value ≥ 0.5` else minor; `≥ 0.70` (vivid) and dark is harmonic minor if `value
< 0.18` or blues minor if `value < 0.42`; otherwise a diatonic mode is picked along
the dark→bright axis over `[Phrygian, Aeolian, Dorian, Mixolydian, Ionian, Lydian]`
(`chooseScaleType`):

$$\text{idx} = \operatorname{clamp}(\lfloor 6\cdot\text{value} \rfloor, 0, 5)$$

### C. Contour & Density

Each map below is a closed-form function of brightness `b` (or a derived feature);
ranges and weights are tuned constants, named where they appear. `brightnessBias`
(surfaced as **Image Influence**) later decides how literally these targets are
followed versus the chain's own preference (see **D**).

**1. Velocity from brightness.** Brightness sets a base velocity in `[50, 115]`,
then Energy scales it (`brightnessToVelocity`, `applyEnergy`):

$$\text{velocity} = \big(50 + \operatorname{round}(65\,b)\big)\cdot(0.7 + 0.6\,\text{energy})$$

**2. Register / contour target.** Brightness maps linearly onto a scale degree
across the whole compass of `N` degrees (`scales/ScaleLibrary.cpp · mapBrightnessToDegree`):

$$\text{degree} = \operatorname{clamp}(\lfloor b\cdot\text{totalDegrees}\rfloor, 0, N-1)$$

**3. Note length (Flowing).** A note length is drawn from three beat values with
brightness-tilted weights — dark leans long, bright leans short (`flowingDuration`):

$$w\big(\{0.5,\ 1.0,\ 2.0\}\ \text{beats}\big) = \{\,0.15 + b,\ \ 0.60,\ \ 0.15 + (1-b)\,\}$$

**4. Rhythmic density.** Local contrast splits a note into up to four equal pieces
(`densitySubdivisionsWanted`):

$$\text{pieces} = 1 + \operatorname{clamp}\!\big(\operatorname{round}(3\,\text{contrast}\cdot\text{amount}),\ 0,\ 3\big)$$

**5. Groove choice.** An activity scalar weights the rhythm templates by cubed
match, so the closest-energy groove dominates (`pickRhythmTemplate`):

$$\text{activity} = \operatorname{clamp}(0.5\,\text{energy} + 0.5\,\text{imageDetail}), \qquad w_i = 0.04 + \text{match}_i^{\,3}, \qquad \text{match}_i = 1 - \Big|\text{activity} - \tfrac{i}{N-1}\Big|$$

**6. Phrase contour.** Central differences `sₓ, s_y` around the start cell give a
slope whose sign selects the arc (`selectContour`):

$$\text{slope} = s_x + s_y \ \Rightarrow\ \begin{cases}\text{Rise} & \text{slope} > 0.03\\[2pt] \text{Fall} & \text{slope} < -0.03\\[2pt] \text{Arch} & \text{otherwise}\end{cases}$$

**7. Motif variation.** A varied repeat transposes by a small image-driven delta,
then reined to ±1 degree to stay recognisable (`varyMotif`):

$$\Delta = \operatorname{clamp}(\text{imageDeg} - \text{anchor},\ -2,\ +2) \quad \text{(then reined to } \pm 1)$$

### D. Markov Selection

Pitch is a first-order, music-theory-weighted Markov walk over scale degrees.
Defaults (`markov/TheoryWeights.h`): `intervalDecay 0.5`, `repeatWeight 0.5`,
`thirdRepeatDamping 0.25`, `tonicGravity 0.4`, `centerGravity 0.3`, `leapThreshold
2`, `leapResolution 4`.

**1. Interval weight.** Stepwise motion is likeliest via geometric decay, so leaps
get rarer with distance `dist = |i−j|` (`markov/TransitionMatrix.cpp · fromTheory`):

$$\text{interval} = \begin{cases}\text{repeatWeight} & \text{dist} = 0\\[2pt] \text{intervalDecay}^{\,\text{dist}-1} & \text{dist} > 0\end{cases}$$

**2. Gravity toward tonic and centre.** Degrees drift gently toward the tonic and
the middle of the range instead of stranding at the extremes (`fromTheory`):

$$\text{gravity} = 1 + \text{tonicGravity}\cdot\text{tonicProx}(j) + \text{centerGravity}\cdot\text{centerProx}(j), \qquad \text{tonicProx}(j) = \frac{1}{1 + d_{\text{tonic}}(j)}, \qquad \text{centerProx}(j) = \max\!\Big(0,\ 1 - \frac{|j - c|}{c}\Big),\ c = \tfrac{N-1}{2}$$

**3. Normalised transition matrix.** The two factors multiply into a row that is
normalised to a distribution (`fromTheory`):

$$P(i \to j) \propto \max(0,\ \text{interval}\cdot\text{gravity}), \qquad \sum_j P(i \to j) = 1$$

**4. Second-order leap resolution.** Each step rebuilds the current row: after a
leap wider than `leapThreshold` the opposite-direction step is boosted, and a
third identical degree is damped (`MelodyChain.cpp · buildDynamicRow`):

$$\text{row}[j] \times (1 + \text{leapResolution}) \quad\text{(post-leap)}, \qquad \text{row}[j] \times \text{thirdRepeatDamping} \quad\text{(third repeat)}$$

**5. Image blend.** The image steers the walk by blending a delta at the
brightness-suggested target into the normalised row, `b = brightnessBias`
(`MelodyChain.cpp · nextBiased`):

$$p = (1 - b)\cdot\text{markovRow} + b\cdot\delta(\text{target})$$

**6. Degree → MIDI note.** The chosen degree becomes a pitch through the scale's
interval table, `dpo` = degrees-per-octave (`scales/Scale.cpp · noteAt`):

$$\text{note} = \text{rootNote} + \text{intervals}[\text{degree} \bmod \text{dpo}] + 12\left\lfloor \text{degree}/\text{dpo}\right\rfloor$$

### E. The Clock

Time is planned **plan-then-walk** on an integer tick grid: every draw that shapes
timing is consumed *before any pitch exists*, and one pop progression is chosen for
the whole session as a single RNG draw over the six templates below (an explicit
**Lock Harmony** draws none), tiled one chord per bar.

**1. Grid factorisation.** 960 ticks per quarter-note (`kTicksPerBeat`) divides
every subdivision the generator emits, so every onset and length is an exact
integer tick and the strong-beat test is exact integer arithmetic:

$$960 = 2^{6}\cdot 3\cdot 5 \ \Rightarrow\ \tfrac{1}{2} = 480,\quad \tfrac{1}{3} = 320,\quad \tfrac{1}{64}\ \text{beat} = 15\ \text{ticks}$$

**2. Bar-aligned duration.** A note starting at `tick` sounds until the next slot
boundary of the session-locked two-bar groove of period `period` (`barAlignedDuration`):

$$\text{duration} = \frac{\text{acc} - (\text{tick} \bmod \text{period})}{960}\ \text{beats}$$

**3. Tied anticipation.** A mid-slot start whose remainder is shorter than an
eighth sustains through the boundary as one MIDI event and is never density-split,
so notes can honestly tie across bar lines (`planTiming`):

$$\big(\text{tick} \bmod \text{period}\big)\ \text{leaves remainder} < \tfrac{1}{8}\ \text{note} \ \Rightarrow\ \text{sustain to end of next slot as one event}$$

**4. Progression choice.** One session draw picks among six four-chord pop
templates, stored as diatonic root degrees `{0=I, 3=IV, 4=V, 5=vi}`
(`pickProgressionBase`, `progressionTemplates`):

$$\text{base} \in \{\,I\text{-}V\text{-}vi\text{-}IV,\ \ vi\text{-}IV\text{-}I\text{-}V,\ \ I\text{-}vi\text{-}IV\text{-}V,\ \ I\text{-}IV\text{-}vi\text{-}V,\ \ I\text{-}IV\text{-}V\text{-}vi,\ \ vi\text{-}IV\text{-}V\text{-}I\,\}$$

**5. Per-bar chord spelling.** Each bar's chord is spelled as a real triad in the
key's parent major `{0,2,4,5,7,9,11}` or minor `{0,2,3,5,7,8,10}`
(`PhraseBuilder::updateHarmonyTarget`):

$$\text{bar} = \left\lfloor\tfrac{\text{tick}}{\text{ticksPerBar}}\right\rfloor,\quad \text{root} = \text{prog}[\text{bar} \bmod n],\quad \text{pc}_k = \big(\text{tonicPc} + \text{steps}[(\text{root} + 2k)\bmod 7]\big)\bmod 12,\ \ k \in \{0,1,2\}$$

**6. Strong-beat chord snap.** On a strong beat an unconditional coin snaps the
drawn degree to the nearest chord tone, so cadences and downbeats land in the
harmony (`walkDegreesAt`):

$$\text{strong} \iff (\text{tick} \bmod 960 = 0)\ \lor\ \text{entry note}; \qquad \text{coin} < 0.6 \Rightarrow \text{snap to nearest chord tone}$$

**7. Phrase form.** `generatePhrased` assembles motifs of `motifLen ∈ [3,5]` notes,
alternating copied A-family slots at odd positions with freshly walked, teleported
**B** phrases at even positions, always reaching at least `A, A′, B` before a
closing phrase (`generatePhrased`):

$$\textbf{while}\ (\text{phraseTotal} < 3\ \lor\ \text{bodyNotes} < \text{target} - \text{motifLen}):\quad \text{odd} \to A\text{-family},\quad \text{even} \to \text{walked } B$$

**8. Motif rein.** `varyMotif` preserves interval content so A′ stays recognisable
— its transpose is reined to ±1 degree and it may lift the whole repeat an octave,
but never two lifts running (`varyMotif`):

$$\text{transpose reined to } \pm 1\ \text{degree}, \qquad \Pr[\text{octave lift}] = 0.25\ \text{(never two consecutive)}$$

**9. Related-region window (C-1).** B teleports to a random cell (two draws) but
reads its *pitch material* from a shadow cell remapped into a window of half-width
`W` around motif A's anchor — a post-draw remap that never perturbs the stream
(`planPhraseCells`, `stepCell`):

$$W = \max\!\Big(1,\ \frac{2\,\text{columns}}{16}\Big)$$

**10. Cadence rules.** Non-closing phrase endings lean to the nearest tonic/fifth
and settle to at least a dotted quarter; the closing note lands on the tonic, held
for 2 or 4 beats — a coin (`kPhraseEndBias`, `drawCadenceLength`):

$$\text{phrase end: bias } 0.85 \to \text{nearest tonic/fifth},\ \ge 1.5\ \text{beats}; \qquad \text{close: tonic held } \in \{2, 4\}\ \text{beats}$$

**11. Open cadence (C-3).** B's last note snaps to the nearest half-cadence degree
— the 2nd (pitch class 2) or 5th (7), tie-break to the fifth — so B opens tension
the next A-family return answers (`openBPhraseCadence`):

$$B_{\text{last}} \to \text{nearest of pitch class } \{2,\ 7\} \quad \text{(tie-break to } 7)$$

**12. Register continuity.** Draw-free, pitch-only: a walked phrase's degrees are
clamped within a compass of the entry note, then whole phrases are octave-folded
toward the previous bar's centroid past a band (6 semitones for A-family/closing, 9
for B), with a final per-bar rescue keeping one degree of edge headroom
(`kPhraseCompassDegrees`, `applyRegisterContinuity`, `kRegisterBandA/B`, `foldGroup`,
`kFoldEdgeMargin`):

$$|\text{degree} - \text{entry}| \le 4\ \text{degrees}, \qquad \text{fold band} = 6\ \text{(A-family)} \mid 9\ \text{(B)}\ \text{semitones}$$

Octave folds preserve pitch classes and interval content, so chord snaps, B's open
cadence and the closing tonic all keep their function.

### F. Determinism

**1. Borrowed engine.** The `std::mt19937` is borrowed and never seeded internally,
so the caller's seed alone determines the stream (`generateMelody`):

$$(\text{image bytes},\ \text{settings},\ \text{seed}) \ \longmapsto\ \text{MIDI} \qquad \text{(byte-identical, any machine)}$$

**2. Pitch-timing firewall.** Every place the image or a melodic choice could bend
the result does so through a post-draw clamp or remap (the compass clamp, register
folds, motif rein, and the C-1 window are all of this form), so the count and order
of draws are a pure function of the timing/structure domain — pinned by
`tests/MelodyGeneratorTests.cpp · test_pitch_domain_never_shifts_timing`:

$$\#\{\text{draws}\},\ \text{order} = f(\text{timing/structure domain only}) \quad\Rightarrow\quad \text{pitch} \not\to \text{RNG stream}$$

**3. Byte-identical serialisation.** A dependency-free Standard MIDI File writer
resolves beat-timed notes to a tick timeline and stably sorts events so note-offs
precede note-ons at an equal tick (`midi/MidiSequence.cpp`):

$$\text{note-off} \prec \text{note-on at equal tick}, \qquad \text{PPQ}_{\text{SMF}} = 480,\quad \text{plan grid} = 960$$

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
