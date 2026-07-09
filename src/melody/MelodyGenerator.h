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

/// Which kind of material a generation pass produces. Melody is the default
/// theory-weighted single line; Chords emits a block-chord progression (stacked
/// scale-thirds sounded together); Arpeggio spells chords out one note at a time
/// in an `ArpPattern`.
enum class GenerationMode {
    Melody,
    Chords,
    Arpeggio,
};

/// The order an arpeggiator cycles through its chord tones (only consulted when
/// MelodyOptions::mode is GenerationMode::Arpeggio).
enum class ArpPattern {
    Up,        ///< Low to high, then wrap back to the lowest.
    Down,      ///< High to low, then wrap back to the highest.
    UpDown,    ///< Up then back down without repeating the turning points.
    Converge,  ///< Outside-in: lowest, highest, next-lowest, next-highest, ...
    Random,    ///< A fresh random chord tone each step (seeded, reproducible).
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
    /// melodies); higher values track image brightness more literally. The
    /// plugin surfaces this as "Image Influence".
    double brightnessBias = 0.25;

    /// Probability, per phrase, of replacing one note with a quick arpeggiated
    /// figure (root/third/fifth of the scale). In [0, 1]; 0 disables ornaments.
    /// Only consulted in Phrased Melody mode. The plugin surfaces this as
    /// "Complexity". Bright cells (> 0.7) are preferred as the note to ornament.
    double arpeggioAmount = 0.15;

    /// Overall intensity, in [0, 1]. Higher values push velocities louder and
    /// thin out the rests between phrases, for a more driving feel; lower values
    /// are softer and more spacious. Surfaced in the plugin as "Energy".
    double energy = 0.5;

    /// How strongly Phrased mode repeats its motif versus varying it, in [0, 1].
    /// 1 repeats the opening motif almost verbatim each phrase; 0 always varies
    /// (the original, more musical behaviour). Kept low by default so melodies
    /// stay varied — Repetition is an opt-in "make the hook recur" control.
    /// Surfaced in the plugin as "Repetition".
    double repetition = 0.2;

    /// What kind of material to generate.
    GenerationMode mode = GenerationMode::Melody;

    /// The cycling order for the arpeggiator (Arpeggio mode).
    ArpPattern arpPattern = ArpPattern::UpDown;

    /// How many octaves the arpeggio chord spans (Arpeggio mode; >= 1).
    int arpOctaves = 2;

    /// Arpeggiator step length in beats (0.25 = sixteenths, 0.5 = eighths).
    double arpRate = 0.5;

    /// Notes per chord in Chords mode: 3 = triad, 4 = seventh (stacked
    /// scale-thirds from the chord root).
    int chordSize = 3;

    /// Length of each chord in beats, in Chords mode (2.0 = half notes).
    double chordRate = 2.0;

    /// Loop length in whole bars: 0 = one-shot (no loop), otherwise 1/2/4/8.
    /// When > 0 the finished material is made a seamless loop of exactly this
    /// many `beatsPerBar` bars, so a looping player repeats it with no gap. In
    /// Arpeggio/Chords mode the note/chord count is aligned to fill the bars; in
    /// Melody mode the final note is extended to the bar line (Phrased already
    /// cadences on the tonic, so the wrap resolves musically).
    int loopBars = 0;

    /// Beats per bar used for loop alignment (4.0 = common time).
    double beatsPerBar = 4.0;

    /// Image-driven rhythmic density, in [0, 1] (Phase 3). 0 disables it: note
    /// durations come purely from the session groove (the Phase-3 bar clock).
    /// Above 0, each phrased note's local image contrast — the brightness range
    /// in the cells around its source cell, i.e. detail/edges — subdivides it
    /// into more, shorter notes: busy regions get denser rhythm, flat regions
    /// stay long. Fully deterministic (a pure function of the grid, no RNG) and
    /// always on the 960-tick grid. Only Phrased Melody mode consults it.
    /// Defaults off so existing output and the RNG draw stream are unchanged;
    /// the plugin will surface it as a "Density" / "Image Rhythm" control.
    double imageRhythmAmount = 0.0;

    /// Explicit chord progression to voice, as diatonic root degrees (0 = I,
    /// 3 = IV, 4 = V, 5 = vi; the 4-degree base is tiled to the needed length).
    /// EMPTY (the default) means "pick one at random from the seed" — exactly the
    /// pre-4b behaviour, drawing one RNG value and leaving every existing baseline
    /// byte-identical. When NON-empty the generator consumes NO RNG for the
    /// progression and voices these roots instead: this backs "Lock Harmony",
    /// where the previous run's progression (carried on `Melody::progression`) is
    /// fed back so pitch/rhythm re-roll under a fixed harmony. Phase 4b.
    std::vector<int> progression;
};

/// The lowest and highest MIDI velocity brightness maps onto.
inline constexpr int kMinVelocity = 50;
inline constexpr int kMaxVelocity = 115;

/// Maps a normalised brightness in [0, 1] to a MIDI velocity in
/// [kMinVelocity, kMaxVelocity]: darker is softer, brighter is louder. Values
/// outside [0, 1] are clamped.
int brightnessToVelocity(float brightness) noexcept;

/// Picks a Flowing-mode note length in beats (0.5 eighth, 1.0 quarter, 2.0
/// half) with weights skewed by `brightness`: darker favours longer notes.
/// Consumes one draw from `rng`.
double flowingDuration(float brightness, std::mt19937& rng);

/// The brightness-grid cell (column, row) a note was sampled from. Columns and
/// rows are 0-based, matching image::BrightnessGrid's row-major layout, so a
/// host can map a note back to the region of the image that produced it (e.g.
/// to highlight that cell while the note sounds).
struct GridCell {
    int col = 0;
    int row = 0;
};

/// A generated melody: the MIDI-ready notes plus, in parallel, the scale-degree
/// index each note landed on. The degree track lets callers reason about
/// melodic intervals in scale steps without inverting the note mapping.
struct Melody {
    std::vector<midi::Note> notes;

    /// The scale degree each note occupies, in parallel with `notes`.
    /// - Melody/Freeform/Phrased: the extended index into the melodic `scale`,
    ///   satisfying `scale.noteAt(degrees[i], octaveSpan) == notes[i].noteNumber`.
    /// - Arp/Chord: `-1`, meaning "not a Melody-scale degree" — these notes are
    ///   voiced from the key's diatonic triads, not the melodic scale, so there
    ///   is no valid index here; their identity lives in `chordTones`. Consumers
    ///   that map a degree back through `scale.noteAt` must skip the sentinel.
    std::vector<int> degrees;

    /// Chord-tone role of each note (0 = root, 1 = third, 2 = fifth, ...), in
    /// parallel with `notes`. Populated only in Arp/Chord modes; empty in the
    /// melodic modes (where notes are not chord tones). Kept as a separate track
    /// rather than overloading `degrees`, so `degrees` keeps a single meaning.
    std::vector<int> chordTones;

    /// The source grid cell for each note, in parallel with `notes` (so
    /// `cells.size() == notes.size()`). A note produced by transposing a motif
    /// (the A'/A'' variations) carries the cell of the motif note it derives
    /// from; the notes of an arpeggio ornament all carry their origin note's
    /// cell. Lets a host trace the melody's path across the image.
    std::vector<GridCell> cells;

    /// In Phrased mode, the index into `notes`/`degrees` where each phrase
    /// begins (the first entry is always 0). Empty in Freeform mode. Lets
    /// callers reason about phrase boundaries — e.g. the rests between them or
    /// where a motif repeats.
    std::vector<std::size_t> phraseStarts;

    /// Diagnostics only (bug-4 measurement hook). Parallel with `notes` in
    /// Phrased Melody mode; empty in every other mode. Never read by generation
    /// or the MIDI path — they exist so a harness can score strong-beat and
    /// chord-tone behaviour without reconstructing the engine's internal clocks.
    /// - dbgStrong:    1 if the note fell on a strong beat when generated, else 0.
    /// - dbgSnapped:   1 if stepNote snapped it to a chord tone (walked phrases
    ///                 only; copied/varied phrases are 0), else 0.
    /// - dbgChordRoot: the progression root degree the snap targeted, or -1.
    std::vector<int> dbgStrong;
    std::vector<int> dbgSnapped;
    std::vector<int> dbgChordRoot;

    /// The chord progression base this melody was voiced over, as diatonic root
    /// degrees (0 = I, 3 = IV, 4 = V, 5 = vi) — the harmonic identity chosen at
    /// generation time. Populated in Phrased, Arpeggio and Chords modes; empty in
    /// Freeform (a flat Markov walk with no chord backbone) and for an empty
    /// melody. Lets a caller carry the harmony forward into a re-generation via
    /// `MelodyOptions::progression` — the mechanism behind "Lock Harmony" (4b).
    std::vector<int> progression;
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

/// Which dimensions of an existing melody a re-generation should preserve.
/// These back the plugin's "Lock Rhythm" / "Lock Pitch" toggles: a locked
/// dimension is carried over verbatim from the previous melody while the
/// unlocked dimension(s) are regenerated. Locking both keeps the melody
/// unchanged; locking neither is a full fresh generation.
struct RegenLocks {
    bool rhythm = false;  ///< Keep each note's start/length (the timing track).
    bool pitch = false;   ///< Keep each note's pitch/degree (the melodic track).
    bool harmony = false; ///< Keep the chord progression (fed via MelodyOptions).
};

/// Combines a `previous` melody with a freshly generated `candidate`, taking the
/// timing track from whichever the locks say and the pitch track from the other.
/// The result follows the timing source's note count; if the pitch source is
/// shorter its degrees are cycled to fill. Velocities and source cells follow
/// the pitch track (they belong to the melodic contour). With neither lock set
/// the candidate is returned unchanged; with both, the previous is returned.
Melody recombineLocked(const Melody& previous, const Melody& candidate,
                       const scales::Scale& scale, RegenLocks locks,
                       const MelodyOptions& options);

/// Produces a small variation of `base`: alters roughly `amount` (in [0, 1]) of
/// the notes, nudging unlocked pitches to a neighbouring scale degree and/or
/// jittering unlocked note lengths, leaving locked dimensions untouched. Used
/// for the plugin's "Mutate" action. `rng` is borrowed, never seeded.
Melody mutate(const Melody& base, const scales::Scale& scale, RegenLocks locks,
              double amount, const MelodyOptions& options, std::mt19937& rng);

}  // namespace lumena::melody
