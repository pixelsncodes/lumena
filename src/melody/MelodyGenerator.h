#pragma once

#include <cstddef>
#include <random>
#include <vector>

#include "image/BrightnessGrid.h"
#include "midi/MidiSequence.h"  // for midi::Note
#include "scales/Scale.h"

namespace lumena::melody {

/// How note durations are chosen.
enum class RhythmMode {
    /// Every note is a quarter note. Steady, metronomic.
    Straight,

    /// Duration is a weighted random pick among eighth/quarter/half notes,
    /// biased by cell brightness: darker cells lean toward longer notes,
    /// brighter cells toward shorter ones. Gives the melody a breathing,
    /// "flowing" feel.
    Flowing,
};

/// How the generator walks the brightness grid to pick a brightness target for
/// each note.
enum class CellPath {
    /// Each note samples a cell adjacent (8-connected, including diagonals) to
    /// the previous note's cell, wrapping at the grid edges. The melody
    /// "wanders across the image", so brightness targets change gradually and
    /// the resulting line is spatially coherent.
    RandomWalk,

    /// Each note samples a grid cell independently and uniformly at random.
    /// Targets jump around, so the melody leaps more.
    PureRandom,
};

/// Tunables for a single melody generation pass.
struct MelodyOptions {
    /// Number of octaves the scale spans (>= 1).
    int octaveSpan = 2;

    /// Number of notes to emit. 0 means "one note per grid cell".
    int length = 0;

    /// Duration strategy (defaults to the more musical Flowing mode).
    RhythmMode rhythm = RhythmMode::Flowing;

    /// Grid-traversal strategy (defaults to the coherent RandomWalk).
    CellPath cellPath = CellPath::RandomWalk;

    /// Pull toward the brightness-suggested scale degree, in [0, 1]. Lower
    /// values let the Markov chain's stepwise preference dominate (smoother
    /// melodies); higher values track image brightness more literally.
    double brightnessBias = 0.25;
};

/// The lowest and highest MIDI velocity brightness maps onto.
inline constexpr int kMinVelocity = 40;
inline constexpr int kMaxVelocity = 115;

/// Maps a normalised brightness in [0, 1] to a MIDI velocity in
/// [kMinVelocity, kMaxVelocity]: darker is softer, brighter is louder. Values
/// outside [0, 1] are clamped.
int brightnessToVelocity(float brightness) noexcept;

/// Picks a Flowing-mode note length in beats (0.5 eighth, 1.0 quarter, 2.0
/// half) with weights skewed by `brightness`: darker favours longer notes.
/// Consumes one draw from `rng`.
double flowingDuration(float brightness, std::mt19937& rng);

/// A generated melody: the MIDI-ready notes plus, in parallel, the scale-degree
/// index each note landed on. The degree track lets callers reason about
/// melodic intervals in scale steps without inverting the note mapping.
struct Melody {
    std::vector<midi::Note> notes;
    std::vector<int> degrees;
};

/// Runs the full melody walk: a theory-weighted Markov chain over scale degrees,
/// steered toward the brightness each visited grid cell suggests, emitting one
/// note per step with brightness-driven velocity and (in Flowing mode)
/// brightness-driven duration. `rng` is borrowed and never seeded internally,
/// so a fixed seed yields a reproducible melody. Never throws.
Melody generateMelody(const image::BrightnessGrid& grid,
                      const scales::Scale& scale, const MelodyOptions& options,
                      std::mt19937& rng);

}  // namespace lumena::melody
