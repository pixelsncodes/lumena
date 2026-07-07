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

/// How notes are organised into higher-level structure.
enum class PhraseMode {
    /// Notes are grouped into musical phrases: a short motif (A), a varied
    /// repeat of it (A'), a contrasting phrase (B) walked from a different grid
    /// region, and a cadential closing phrase that lands on the tonic. Longer
    /// sequences extend the pattern (A A' B A'' B ...). Rests may fall between
    /// phrases, phrase endings lean toward tonic/fifth, and phrases may sprout
    /// arpeggio ornaments. This is the more musical default.
    Phrased,

    /// The original flat note stream: one continuous theory-weighted walk with
    /// no phrase boundaries, rests, cadence or ornaments.
    Freeform,
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

    /// Phrase-structure strategy (defaults to the more musical Phrased mode).
    /// In Phrased mode `length` is an approximate target: the generator emits
    /// whole phrases until it reaches it, then always appends a closing phrase,
    /// so the final count may run slightly over (and ornaments add notes too).
    PhraseMode phraseMode = PhraseMode::Phrased;

    /// Pull toward the brightness-suggested scale degree, in [0, 1]. Lower
    /// values let the Markov chain's stepwise preference dominate (smoother
    /// melodies); higher values track image brightness more literally.
    double brightnessBias = 0.25;

    /// Probability, per phrase, of replacing one note with a quick arpeggiated
    /// figure (root/third/fifth of the pentatonic). In [0, 1]; 0 disables
    /// ornaments. Only consulted in Phrased mode. Bright cells (> 0.7) are
    /// preferred as the note to ornament.
    double arpeggioAmount = 0.15;
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

    /// In Phrased mode, the index into `notes`/`degrees` where each phrase
    /// begins (the first entry is always 0). Empty in Freeform mode. Lets
    /// callers reason about phrase boundaries — e.g. the rests between them or
    /// where a motif repeats.
    std::vector<std::size_t> phraseStarts;
};

/// Runs the full melody walk: a theory-weighted Markov chain over scale degrees,
/// steered toward the brightness each visited grid cell suggests, emitting one
/// note per step with brightness-driven velocity and (in Flowing mode)
/// brightness-driven duration.
///
/// In Phrased mode (the default) the notes are further organised into motif /
/// variation / contrast / closing phrases, with optional rests between phrases,
/// tonic/fifth-leaning phrase endings, a tonic cadence, and arpeggio ornaments;
/// `Melody::phraseStarts` records the boundaries. In Freeform mode the output is
/// a single flat walk with no such structure.
///
/// `rng` is borrowed and never seeded internally, so a fixed seed yields a
/// reproducible melody. Never throws.
Melody generateMelody(const image::BrightnessGrid& grid,
                      const scales::Scale& scale, const MelodyOptions& options,
                      std::mt19937& rng);

}  // namespace lumena::melody
