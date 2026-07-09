#include "melody/MelodyGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <unordered_map>

#include "markov/MelodyChain.h"
#include "markov/TheoryWeights.h"
#include "markov/TransitionMatrix.h"
#include "scales/ScaleLibrary.h"  // mapBrightnessToDegree

namespace lumena::melody {

namespace {

using image::BrightnessGrid;
using markov::MelodyChain;
using markov::TheoryWeights;
using markov::TransitionMatrix;
using midi::Note;
using scales::mapBrightnessToDegree;
using scales::Scale;

float clamp01(float v) noexcept {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

// Wraps `value` into [0, span) even when it stepped one past either edge.
int wrap(int value, int span) noexcept {
    if (span <= 0) return 0;
    return ((value % span) + span) % span;
}

// The eight (dx, dy) offsets around a cell, in a fixed order.
constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
constexpr int kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

// ---- phrased-mode constants -------------------------------------------------

// Probability of a rest at each phrase boundary.
constexpr double kRestProbability = 0.6;
// A phrase ending leans this hard toward the nearest tonic/fifth degree.
constexpr float kPhraseEndBias = 0.85f;
// The cadence (final) note is at least this long, in beats (a half note).
constexpr double kCadenceBeats = 2.0;
// Cells at least this bright are preferred as arpeggio ornament sites.
constexpr float kArpeggioBrightness = 0.7f;
// Tick resolution for strong-beat detection: beat positions are quantised to an
// integer tick grid so the on-beat test is exact integer arithmetic, not a
// float epsilon compare. 960 PPQ divides all current durations (multiples of
// 0.5 -> 480 ticks) cleanly.
constexpr long kTicksPerBeat = 960;

// ---- phrase-dynamics (contour) constants ------------------------------------

// Velocity a phrase contour reaches at its soft start/taper and at its peak.
constexpr int kContourVelLow = 62;
constexpr int kContourVelHigh = 112;
// How the final velocity blends the phrase contour with the note's own cell
// brightness (weights sum to 1). Contour dominates so uniformly-bright images
// no longer come out wall-to-wall fortissimo; brightness still colours it.
constexpr double kContourWeight = 0.7;
constexpr double kBrightnessWeight = 0.3;
// Final velocity is clamped to this window.
constexpr int kDynamicMin = 35;
constexpr int kDynamicMax = 115;
// The arc peaks ~2/3 of the way through a phrase, jittered +/- this much.
constexpr double kPeakFraction = 2.0 / 3.0;
constexpr double kPeakJitter = 0.08;
// Arc height (relative to the peak) a normal phrase eases back to at its close.
constexpr double kPhraseEndArc = 0.2;
// The last phrase of the piece is scaled down and tapers fully: a decrescendo.
constexpr double kFinalPhraseScale = 0.85;
// Probability an A'/A'' repeat is hoisted up an octave for lift.
constexpr double kOctaveLiftProbability = 0.25;

}  // namespace

int brightnessToVelocity(float brightness) noexcept {
    const float b = clamp01(brightness);
    const int span = kMaxVelocity - kMinVelocity;
    return kMinVelocity +
           static_cast<int>(std::lround(static_cast<double>(b) * span));
}

namespace {
// Clamps to [0, 1].
double clampUnit(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

// Scales a MIDI velocity by an Energy amount in [0, 1]: 0.5 is neutral, 0 is
// softer (down to ~0.7x), 1 is louder (up to ~1.3x). Result stays in [1, 127].
int applyEnergy(int velocity, double energy) {
    const double factor = 0.7 + 0.6 * clampUnit(energy);
    const long v = std::lround(static_cast<double>(velocity) * factor);
    return static_cast<int>(v < 1 ? 1 : (v > 127 ? 127 : v));
}
}  // namespace

double flowingDuration(float brightness, std::mt19937& rng) {
    const double b = static_cast<double>(clamp01(brightness));

    // Candidate note lengths in beats and their brightness-skewed weights.
    // Bright cells lean short (eighths); dark cells lean long (halves); the
    // quarter keeps a steady baseline so no length is ever impossible.
    const double lengths[3] = {0.5, 1.0, 2.0};
    const double weights[3] = {0.15 + b, 0.60, 0.15 + (1.0 - b)};

    double sum = weights[0] + weights[1] + weights[2];
    std::uniform_real_distribution<double> dist(0.0, sum);
    double r = dist(rng);
    double cumulative = 0.0;
    for (int i = 0; i < 3; ++i) {
        cumulative += weights[i];
        if (r <= cumulative) {
            return lengths[i];
        }
    }
    return lengths[2];  // floating-point slack
}

namespace {

// Semitone offsets of the major and natural-minor scales from the tonic. Used
// both by the phrased melody's per-bar harmonic target and by the chord/arp
// generators to spell real triads in the key's parent major/minor.
constexpr int kMajorSteps[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kMinorSteps[7] = {0, 2, 3, 5, 7, 8, 10};

// A session-locked named pop progression (I/IV/V/vi), tiled to `count` chords.
// Defined below; forward-declared so the phrased melody can target it too.
std::vector<int> progressionRoots(int count, std::mt19937& rng);

// The original flat walk: a single continuous theory-weighted Markov walk,
// steered by grid brightness. Kept byte-for-byte so a fixed seed reproduces the
// pre-phrase-structure behaviour.
Melody generateFreeform(const BrightnessGrid& grid, const Scale& scale,
                        const MelodyOptions& options, MelodyChain& chain,
                        int totalDegrees, std::mt19937& rng) {
    Melody melody;

    const int cols = grid.columns();
    const int rows = grid.rows();

    // Start in the middle of the range so the walk has room in both directions.
    std::size_t degree = static_cast<std::size_t>(totalDegrees / 2);

    // Start the grid walk at the centre cell.
    int col = cols / 2;
    int row = rows / 2;

    const int length =
        options.length > 0 ? options.length : static_cast<int>(grid.cellCount());
    melody.notes.reserve(static_cast<std::size_t>(length));
    melody.degrees.reserve(static_cast<std::size_t>(length));
    melody.cells.reserve(static_cast<std::size_t>(length));

    std::uniform_int_distribution<int> colDist(0, cols - 1);
    std::uniform_int_distribution<int> rowDist(0, rows - 1);
    std::uniform_int_distribution<int> stepDist(0, 7);  // 8-connected neighbour

    const float bias = static_cast<float>(options.brightnessBias);

    double beat = 0.0;
    for (int i = 0; i < length; ++i) {
        if (options.cellPath == CellPath::PureRandom) {
            col = colDist(rng);
            row = rowDist(rng);
        } else if (i > 0) {
            // RandomWalk: the first note stays on the centre cell; every step
            // after moves to one of the eight adjacent cells, wrapping edges.
            const int k = stepDist(rng);
            col = wrap(col + kDx[k], cols);
            row = wrap(row + kDy[k], rows);
        }

        const float brightness = grid.valueAt(col, row);
        const int target = mapBrightnessToDegree(brightness, totalDegrees);
        degree = chain.nextBiased(degree, static_cast<float>(target), bias);

        Note note;
        note.noteNumber = scale.noteAt(static_cast<int>(degree), options.octaveSpan);
        note.velocity = applyEnergy(brightnessToVelocity(brightness), options.energy);
        note.startBeats = beat;
        note.lengthBeats = (options.rhythm == RhythmMode::Flowing)
                               ? flowingDuration(brightness, rng)
                               : 1.0;

        melody.notes.push_back(note);
        melody.degrees.push_back(static_cast<int>(degree));
        melody.cells.push_back(GridCell{col, row});
        beat += note.lengthBeats;
    }

    return melody;
}

// ---- phrased-mode helpers ---------------------------------------------------

// A note carrying enough context to build phrases before it is timed and
// serialised: its scale degree, resolved pitch, velocity, duration and the
// brightness of the cell that produced it (used to choose ornament sites).
struct PhraseNote {
    int degree = 0;
    int noteNumber = 0;
    int velocity = 0;
    double lengthBeats = 1.0;
    float brightness = 0.0f;
    // The grid cell this note was sampled from. Preserved through motif
    // variation (A'/A'' copy the motif's cells) and ornamentation (an arpeggio
    // figure inherits its origin note's cell), so it survives into Melody::cells.
    int col = 0;
    int row = 0;

    // ---- diagnostics only (bug-4 measurement hook) --------------------------
    // Never read by generation or the MIDI path; propagated to Melody's debug
    // tracks purely so the harness can score strong-beat / chord-tone behaviour.
    // Set on notes produced by stepNote (walked phrases); copied/varied phrases
    // clear them (they are not snapped in their new position).
    bool dbgStrong = false;   ///< was this a strong beat when generated?
    bool dbgSnapped = false;  ///< did stepNote snap it to a chord tone?
    int dbgChordRoot = -1;    ///< progression root degree the snap targeted (-1 = none)

    // ---- pass-2 re-harmonisation inputs (bug 4, 4a two-pass) ----------------
    // Recorded in pass 1 so the pass-2 re-harmonisation can decide the snap
    // against the note's REAL emitted beat without re-running the walk (and
    // without drawing any rng). `eligible` marks walked, non-ending notes — the
    // only ones the snap ever touches; copied/varied/cadence/ending notes are
    // false and are left exactly as generated.
    double snapCoin = 1.0;   ///< the unconditional snap coin drawn in pass 1 (>=0.6 => never snaps)
    bool eligible = false;   ///< walked non-ending note (a snap site candidate)
};

// State the phrase builder threads through generation: the grid walk position,
// the current scale degree, and the shared musical parameters.
class PhraseBuilder {
public:
    PhraseBuilder(const BrightnessGrid& grid, const Scale& scale,
                  const MelodyOptions& options, MelodyChain& chain,
                  int totalDegrees, std::mt19937& rng)
        : grid_(grid),
          scale_(scale),
          options_(options),
          chain_(chain),
          totalDegrees_(totalDegrees),
          degreesPerOctave_(static_cast<int>(scale.degreesPerOctave())),
          rng_(rng),
          bias_(static_cast<float>(options.brightnessBias)),
          col_(grid.columns() / 2),
          row_(grid.rows() / 2),
          degree_(static_cast<std::size_t>(totalDegrees / 2)) {
        // Tonic triad pitch classes for strong-beat chord-tone targeting: major
        // or minor third per the scale, plus the perfect fifth.
        bool hasM3 = false, hasm3 = false;
        for (int iv : scale.intervals) {
            const int pc = ((iv % 12) + 12) % 12;
            if (pc == 4) hasM3 = true;
            if (pc == 3) hasm3 = true;
        }
        const int tonicPc = ((scale.rootNote % 12) + 12) % 12;
        const int third = (hasM3 && ! hasm3) ? 4 : 3;
        chordPcs_[0] = tonicPc;
        chordPcs_[1] = (tonicPc + third) % 12;
        chordPcs_[2] = (tonicPc + 7) % 12;

        // Harmonic backbone: the melody now outlines a moving chord progression
        // (the same named pop templates the arp/chords use) rather than pulling
        // toward a single static tonic triad. `progMajor_` picks the parent
        // major/minor the triads are spelled from — see scaleIsMajor().
        tonicPc_ = tonicPc;
        progMajor_ = hasM3 && ! hasm3;
        beatsPerBar_ = options.beatsPerBar > 0.0 ? options.beatsPerBar : 4.0;
        progression_ = progressionRoots(4, rng);
        updateHarmonyTarget(0.0);  // start on the progression's first chord
        pickRhythmTemplate();      // lock one groove for the session
    }

    // Rebuilds the strong-beat chord-tone target (chordPcs_) for the bar that
    // `beat` falls in: one chord per bar, cycling through the session's
    // progression. Triads are spelled from the parent major/minor so they are
    // real chords even when the melodic scale is pentatonic.
    void updateHarmonyTarget(double beat) {
        if (progression_.empty()) return;
        // Quantise the beat to the tick grid before bucketing into bars, exactly
        // as `strong` is (bug 4, 4a). A start beat accumulated through triplet
        // (1/3) ornament durations can land a hair below an integer
        // (e.g. 87.99999999999998); raw (int)(beat/bpb) truncation then put such
        // a note one bar early, snapping it to the wrong chord.
        const long tick = std::lround(beat * kTicksPerBeat);
        const long ticksPerBar =
            std::lround(static_cast<double>(kTicksPerBeat) * beatsPerBar_);
        const int bars =
            ticksPerBar > 0 ? static_cast<int>(tick / ticksPerBar) : 0;
        const int n = static_cast<int>(progression_.size());
        const int idx = ((bars % n) + n) % n;
        const int root = progression_[static_cast<std::size_t>(idx)];
        curChordRoot_ = root;  // diagnostics only (bug-4 hook)
        const int* steps = progMajor_ ? kMajorSteps : kMinorSteps;
        for (int k = 0; k < 3; ++k) {
            const int deg = ((root + 2 * k) % 7 + 7) % 7;
            chordPcs_[k] = ((tonicPc_ + steps[deg]) % 12 + 12) % 12;
        }
    }

    // Builds a phrase of `count` notes by walking the grid. When `newRegion` is
    // set, the walk teleports to a random cell first (the contrasting-phrase B).
    // When `tonicFifthEnding` is set, the last note is pulled hard toward the
    // nearest tonic or fifth degree.
    std::vector<PhraseNote> walkPhrase(int count, bool newRegion,
                                       bool tonicFifthEnding) {
        localBeat_ = 0.0;  // strong-beat tracking is per-phrase
        std::vector<PhraseNote> phrase;
        phrase.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            const bool jump = newRegion && i == 0;
            const bool ending = tonicFifthEnding && i == count - 1;
            phrase.push_back(stepNote(jump, ending));
        }
        return phrase;
    }

    // A closing phrase: `count` notes whose final note is forced onto the
    // nearest tonic (any octave) and held for at least a half note.
    std::vector<PhraseNote> closingPhrase(int count) {
        std::vector<PhraseNote> phrase = walkPhrase(count - 1, /*newRegion=*/false,
                                                    /*tonicFifthEnding=*/true);
        // Land squarely on a tonic for the cadence, approached smoothly.
        const int tonic = nearestDegreeWithPitchClass(static_cast<int>(degree_),
                                                       /*wantFifth=*/false);

        // Ending polish: approach the final tonic by a single scale step from
        // above or below rather than a leap, whenever the range allows. Retune
        // the penultimate note to tonic +/- 1, preferring the step nearer to
        // where the walk already left it so the contour stays smooth.
        if (!phrase.empty()) {
            const bool aboveOk = tonic + 1 < totalDegrees_;
            const bool belowOk = tonic - 1 >= 0;
            if (aboveOk || belowOk) {
                PhraseNote& approach = phrase.back();
                int stepDegree;
                if (aboveOk && belowOk) {
                    stepDegree = std::abs((tonic + 1) - approach.degree) <=
                                         std::abs((tonic - 1) - approach.degree)
                                     ? tonic + 1
                                     : tonic - 1;
                } else {
                    stepDegree = aboveOk ? tonic + 1 : tonic - 1;
                }
                approach.degree = stepDegree;
                approach.noteNumber =
                    scale_.noteAt(stepDegree, options_.octaveSpan);
            }
        }

        degree_ = static_cast<std::size_t>(tonic);
        const float brightness = grid_.valueAt(col_, row_);
        PhraseNote note;
        note.degree = tonic;
        note.noteNumber = scale_.noteAt(tonic, options_.octaveSpan);
        note.velocity = brightnessToVelocity(brightness);
        note.brightness = brightness;
        note.col = col_;
        note.row = row_;
        // A half note, or occasionally a whole note, to breathe at the end.
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        note.lengthBeats = coin(rng_) < 0.5 ? kCadenceBeats : 2.0 * kCadenceBeats;
        phrase.push_back(note);
        return phrase;
    }

    // Transposes a stored motif by +/-1..2 scale degrees (clamped so every note
    // stays in range) and nudges one note's rhythm, yielding the A' variation.
    // Interval content is preserved, so the motif stays recognisable.
    std::vector<PhraseNote> varyMotif(const std::vector<PhraseNote>& motif) {
        std::vector<PhraseNote> v = motif;
        if (v.empty()) return v;

        int lo = std::numeric_limits<int>::max();
        int hi = std::numeric_limits<int>::min();
        for (const PhraseNote& n : v) {
            lo = std::min(lo, n.degree);
            hi = std::max(hi, n.degree);
        }

        static constexpr int kDeltas[4] = {-2, -1, 1, 2};
        std::uniform_int_distribution<int> pick(0, 3);
        int delta = kDeltas[pick(rng_)];
        // Shrink the shift toward 0 until the whole motif fits in range.
        while (delta != 0 && (lo + delta < 0 || hi + delta >= totalDegrees_)) {
            delta += (delta > 0) ? -1 : 1;
        }
        for (PhraseNote& n : v) {
            n.degree += delta;
            n.noteNumber = scale_.noteAt(n.degree, options_.octaveSpan);
        }

        // Octave lift: occasionally hoist the whole repeat up an octave when the
        // range has room. A classic cheap trick that adds lift and direction;
        // shifting every note by an octave preserves the motif's interval
        // content, so A'/A'' stays recognisable.
        std::uniform_real_distribution<double> lift(0.0, 1.0);
        if (lift(rng_) < kOctaveLiftProbability) {
            int hi2 = std::numeric_limits<int>::min();
            for (const PhraseNote& n : v) hi2 = std::max(hi2, n.degree);
            if (hi2 + degreesPerOctave_ < totalDegrees_) {
                for (PhraseNote& n : v) {
                    n.degree += degreesPerOctave_;
                    n.noteNumber = scale_.noteAt(n.degree, options_.octaveSpan);
                }
            }
        }

        // Slight rhythmic variation: retime one note to a neighbouring length.
        if (options_.rhythm == RhythmMode::Flowing) {
            std::uniform_int_distribution<std::size_t> which(0, v.size() - 1);
            const std::size_t j = which(rng_);
            static constexpr double kLengths[3] = {0.5, 1.0, 2.0};
            std::uniform_int_distribution<int> rl(0, 2);
            v[j].lengthBeats = kLengths[rl(rng_)];
        }
        return v;
    }

    // Replaces one note of `phrase` with a three-note arpeggio (scale degrees
    // 0-2-4 of the pentatonic: root/third-ish/fifth-ish) with probability
    // `options.arpeggioAmount`. Bright cells (> 0.7) are preferred as the site;
    // otherwise the brightest note in the phrase is used. In-scale by
    // construction, since every figure note is a real scale degree.
    void maybeOrnament(std::vector<PhraseNote>& phrase) {
        const double amount = std::min(1.0, std::max(0.0, options_.arpeggioAmount));
        if (amount <= 0.0 || phrase.empty()) return;

        std::uniform_real_distribution<double> coin(0.0, 1.0);
        if (coin(rng_) >= amount) return;

        // Prefer a bright cell; fall back to the brightest note in the phrase.
        std::size_t site = 0;
        bool haveBright = false;
        float best = -1.0f;
        for (std::size_t i = 0; i < phrase.size(); ++i) {
            const float b = phrase[i].brightness;
            if (b >= kArpeggioBrightness && !haveBright) {
                site = i;
                haveBright = true;
            }
            if (!haveBright && b > best) {
                best = b;
                site = i;
            }
        }

        const PhraseNote src = phrase[site];
        // Choose a direction that keeps 0-2-4 inside the usable range.
        const bool ascOk = src.degree + 4 < totalDegrees_;
        const bool descOk = src.degree - 4 >= 0;
        if (!ascOk && !descOk) return;  // no room for a figure
        std::uniform_int_distribution<int> dirPick(0, 1);
        bool ascending = ascOk && (!descOk || dirPick(rng_) == 0);
        const int step = ascending ? +2 : -2;

        // Three straight eighths, or an eighth-note triplet spanning one beat.
        std::uniform_int_distribution<int> shape(0, 1);
        const double each = shape(rng_) == 0 ? 0.5 : (1.0 / 3.0);

        std::vector<PhraseNote> figure;
        figure.reserve(3);
        for (int k = 0; k < 3; ++k) {
            const int deg = src.degree + step * k;
            PhraseNote n;
            n.degree = deg;
            n.noteNumber = scale_.noteAt(deg, options_.octaveSpan);
            n.velocity = src.velocity;
            n.brightness = src.brightness;
            n.col = src.col;
            n.row = src.row;
            n.lengthBeats = each;
            figure.push_back(n);
        }
        phrase.erase(phrase.begin() + static_cast<std::ptrdiff_t>(site));
        phrase.insert(phrase.begin() + static_cast<std::ptrdiff_t>(site),
                      figure.begin(), figure.end());
    }

    // Overwrites every note's velocity with a phrase-shaped dynamic contour: a
    // soft start rising to a peak ~2/3 of the way through, then easing back
    // down. The contour (weight 0.7) is blended with each note's own cell
    // brightness (weight 0.3) so colour still tints the dynamics, then clamped.
    // The peak position is lightly randomised per phrase and kept in the
    // interior, so the loudest note is never the first or last of the phrase.
    // The final phrase of the piece is scaled down and tapers fully — a closing
    // decrescendo — which is why uniformly-bright images no longer play
    // wall-to-wall fortissimo.
    void applyPhraseDynamics(std::vector<PhraseNote>& phrase, bool finalPhrase) {
        if (phrase.empty()) return;
        const std::size_t n = phrase.size();

        std::uniform_real_distribution<double> jitter(-kPeakJitter, kPeakJitter);
        double peakFrac = kPeakFraction + jitter(rng_);
        // Keep the peak strictly interior so the arc never crests on an edge.
        if (peakFrac < 0.34) peakFrac = 0.34;
        if (peakFrac > 0.85) peakFrac = 0.85;

        const double endArc = finalPhrase ? 0.0 : kPhraseEndArc;
        const double scale = finalPhrase ? kFinalPhraseScale : 1.0;
        const double vspan = kContourVelHigh - kContourVelLow;

        for (std::size_t i = 0; i < n; ++i) {
            const double t = n > 1 ? static_cast<double>(i) /
                                         static_cast<double>(n - 1)
                                   : 0.0;
            // Arc in [0, 1]: 0 at the start, 1 at the peak, `endArc` at the end.
            double arc;
            if (t <= peakFrac) {
                arc = peakFrac > 0.0 ? t / peakFrac : 1.0;
            } else {
                arc = 1.0 - (1.0 - endArc) * ((t - peakFrac) / (1.0 - peakFrac));
            }
            const double contourVel = (kContourVelLow + arc * vspan) * scale;
            const double brightVel =
                static_cast<double>(brightnessToVelocity(phrase[i].brightness));
            const double v =
                kContourWeight * contourVel + kBrightnessWeight * brightVel;
            int vi = static_cast<int>(std::lround(v));
            if (vi < kDynamicMin) vi = kDynamicMin;
            if (vi > kDynamicMax) vi = kDynamicMax;
            phrase[i].velocity = vi;
        }
    }

    // True at probability `kRestProbability`; the caller inserts a rest.
    bool rollRest() {
        std::uniform_real_distribution<double> coin(0.0, 1.0);
        return coin(rng_) < kRestProbability;
    }

    // An eighth- or quarter-note rest, chosen 50/50.
    double restLength() {
        std::uniform_int_distribution<int> pick(0, 1);
        return pick(rng_) == 0 ? 0.5 : 1.0;
    }

    // Pass 2 of the 4a two-pass. Pass 1 (the walk + flatten above) established
    // every note's REAL emitted start beat, including rests and ornaments; here
    // we re-decide the strong beat and chord-tone snap against THAT beat rather
    // than the generation-time clock, for the walked snap-site candidates only
    // (`eligible`). Draws no rng — it reuses the pass-1 snap coin — so it moves
    // pitches only; timing and the RNG stream are untouched. The dbg* tracks are
    // overwritten with the real-beat verdicts so the harness scores pass 2.
    // `strong` stays every-integer-beat (4b redefines it to bar-relative later).
    void reharmonizeAgainstRealBeats(Melody& melody,
                                     const std::vector<double>& snapCoin,
                                     const std::vector<char>& eligible) {
        const std::size_t n = melody.notes.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (i >= eligible.size() || !eligible[i]) continue;
            const double realBeat = melody.notes[i].startBeats;
            updateHarmonyTarget(realBeat);  // chordPcs_/curChordRoot_ for the real bar
            const long tick = std::lround(realBeat * kTicksPerBeat);
            const bool strong = (tick % kTicksPerBeat) == 0;
            const bool snapped = strong && snapCoin[i] < 0.6;
            if (snapped) {
                const int nd = nearestChordToneDegree(melody.degrees[i]);
                melody.degrees[i] = nd;
                melody.notes[i].noteNumber = scale_.noteAt(nd, options_.octaveSpan);
            }
            melody.dbgStrong[i] = strong ? 1 : 0;
            melody.dbgSnapped[i] = snapped ? 1 : 0;
            melody.dbgChordRoot[i] = snapped ? curChordRoot_ : -1;
        }
    }

private:
    // Advances one step: moves on the grid, then chooses the next degree by
    // stepwise Markov motion nudged by the image's brightness *gradient* (so the
    // image shapes contour without collapsing the line to one pitch), snapping to
    // a chord tone on strong beats. Duration comes from Energy/Complexity, not
    // brightness, so "energetic" is actually denser and more varied.
    PhraseNote stepNote(bool jump, bool ending) {
        const float prevBright = grid_.valueAt(col_, row_);
        if (jump || options_.cellPath == CellPath::PureRandom) {
            std::uniform_int_distribution<int> colDist(0, grid_.columns() - 1);
            std::uniform_int_distribution<int> rowDist(0, grid_.rows() - 1);
            col_ = colDist(rng_);
            row_ = rowDist(rng_);
        } else {
            std::uniform_int_distribution<int> stepDist(0, 7);
            const int k = stepDist(rng_);
            col_ = wrap(col_ + kDx[k], grid_.columns());
            row_ = wrap(row_ + kDy[k], grid_.rows());
        }

        const float brightness = grid_.valueAt(col_, row_);
        const float gradient = brightness - prevBright;
        const double energy = clampUnit(options_.energy);
        const double complexity = clampUnit(options_.arpeggioAmount);
        // Strong-beat = every integer beat, on an exact tick grid (was a fragile
        // float epsilon compare). Classification is unchanged. NOTE: `strong` is
        // still phrase-relative via localBeat_ (resets each phrase) — making it
        // bar-relative and reconciling the localBeat_/harmonyBeat_ clocks is
        // deferred to bug 4.
        const long tick = std::lround(localBeat_ * kTicksPerBeat);
        const bool strong = (tick % kTicksPerBeat) == 0;

        // Advance the harmony to the chord for the current bar, so strong-beat
        // chord-tone targeting outlines the moving progression, not a fixed tonic.
        updateHarmonyTarget(harmonyBeat_);

        bool didSnap = false;   // diagnostics only (bug-4 hook)
        double snapCoinVal = 1.0;  // pass-2 input: the snap coin for this note
        if (ending) {
            const int target = nearestDegreeWithPitchClass(
                static_cast<int>(degree_), /*wantFifth=*/true);
            degree_ = chain_.nextBiased(degree_, static_cast<float>(target),
                                        kPhraseEndBias);
        } else {
            // Base motion: pure Markov (stepwise, with the chain's built-in
            // anti-repeat / voice-leading rules).
            int next = static_cast<int>(chain_.next(degree_));

            // Complexity: occasional leap for interval variety.
            if (uni01() < complexity * 0.4) {
                const int leap = (uni01() < 0.5 ? -1 : 1) *
                                 (2 + static_cast<int>(uni01() * 2.0));  // ±2..3
                next = clampDegree(next + leap);
            }
            // Image influence: nudge one step in the brightness-gradient
            // direction (contour), scaled by the Image Influence amount.
            if (std::fabs(gradient) > 0.02f && uni01() < bias_) {
                next = clampDegree(next + (gradient > 0.0f ? 1 : -1));
            }
            // Strong beats prefer a chord tone; weak beats may pass through.
            // The snap coin is drawn UNCONDITIONALLY (not short-circuited behind
            // `strong`) so the RNG stream no longer depends on the strong-beat
            // classification. This decouples the draw sequence from the upcoming
            // clock/strong changes (bug 4: 4a/4b), so their output movement is
            // attributable to logic, not to RNG re-alignment. This commit (4a-0)
            // is a one-time RNG-ordering churn only — no clock or classification
            // change. See DECISIONS / bug-4 notes.
            snapCoinVal = uni01();
            if (strong && snapCoinVal < 0.6) {
                next = nearestChordToneDegree(next);
                didSnap = true;  // diagnostics only (bug-4 hook)
            }
            // Anti-stuck: never sit on the same degree twice running. The coin is
            // drawn UNCONDITIONALLY (like the snap coin in 4a-0), so the RNG
            // stream no longer depends on whether this branch fires. Required
            // before 4a: the clock fix changes snapped degrees, which changes
            // `next`, which would otherwise flip this short-circuit and perturb
            // the draw sequence. 4a-0b: RNG-ordering churn only, no logic change.
            const double antiStuckCoin = uni01();
            if (next == static_cast<int>(degree_)) {
                next = clampDegree(next + (antiStuckCoin < 0.5 ? -1 : 1));
            }
            degree_ = static_cast<std::size_t>(next);
        }

        PhraseNote note;
        note.degree = static_cast<int>(degree_);
        note.noteNumber = scale_.noteAt(note.degree, options_.octaveSpan);
        int vel = brightnessToVelocity(brightness);
        if (strong) vel = std::min(127, vel + static_cast<int>(10.0 * energy));  // accent
        note.velocity = vel;
        note.brightness = brightness;
        note.col = col_;
        note.row = row_;
        note.dbgStrong = strong;           // diagnostics only (bug-4 hook)
        note.dbgSnapped = didSnap;         // diagnostics only
        note.dbgChordRoot = curChordRoot_; // diagnostics only
        note.snapCoin = snapCoinVal;       // pass-2 input (bug 4, 4a)
        note.eligible = !ending;           // walked non-ending -> a snap site candidate
        note.lengthBeats = (options_.rhythm == RhythmMode::Flowing)
                               ? nextDuration()
                               : 1.0;
        localBeat_ += note.lengthBeats;
        harmonyBeat_ += note.lengthBeats;  // accumulates across phrases
        return note;
    }

    // A uniform [0,1) draw.
    double uni01() {
        std::uniform_real_distribution<double> d(0.0, 1.0);
        return d(rng_);
    }

    // Clamps a degree index into the usable range.
    int clampDegree(int d) const {
        if (d < 0) return 0;
        if (d >= totalDegrees_) return totalDegrees_ - 1;
        return d;
    }

    // The chord tone (tonic-triad pitch class) nearest to degree `from`.
    int nearestChordToneDegree(int from) const {
        for (int r = 0; r < totalDegrees_; ++r) {
            for (int s : {from - r, from + r}) {
                if (s < 0 || s >= totalDegrees_) continue;
                const int pc = scale_.noteAt(s, options_.octaveSpan) % 12;
                if (pc == chordPcs_[0] || pc == chordPcs_[1] || pc == chordPcs_[2])
                    return s;
            }
        }
        return clampDegree(from);
    }

    // Next note length in beats. The session picks one rhythm template up front
    // (see pickRhythmTemplate) and the melody cycles through it, so the piece has
    // a consistent rhythmic identity instead of re-rolling every note. The
    // template's note density is chosen to match Energy, keeping the old
    // "energetic = busier" feel. Returns a steady quarter if no template is set.
    double nextDuration() {
        if (rhythmTemplate_.empty()) return 1.0;
        const double d = rhythmTemplate_[rhythmCursor_ % rhythmTemplate_.size()];
        ++rhythmCursor_;
        return d;
    }

    // Chooses one one-bar rhythm template for the whole session, weighted so
    // higher Energy favours busier grooves. Each template's durations sum to a
    // bar (beatsPerBar_), so cycling it tiles cleanly.
    void pickRhythmTemplate() {
        struct RhythmTemplate { std::vector<double> beats; };
        static const std::vector<RhythmTemplate> kTemplates = {
            {{2.0, 1.0, 1.0}},                        // half + two quarters (3)
            {{1.0, 1.0, 1.0, 1.0}},                   // steady quarters      (4)
            {{1.5, 0.5, 1.0, 1.0}},                   // charleston           (4)
            {{1.5, 0.5, 1.5, 0.5}},                   // dotted swing         (4)
            {{1.0, 0.5, 0.5, 1.0, 1.0}},              // rock eighths         (5)
            {{0.5, 1.0, 0.5, 1.0, 1.0}},              // off-beat push        (5)
            {{1.0, 0.5, 0.5, 1.0, 0.5, 0.5}},         // gallop               (6)
            {{0.5, 0.5, 0.5, 0.5, 1.0, 1.0}},         // driving sixteenths   (6)
        };
        const double e = clampUnit(options_.energy);
        std::vector<double> weights;
        weights.reserve(kTemplates.size());
        for (const RhythmTemplate& t : kTemplates) {
            // Density normalised to ~[0,1]: 3 notes/bar -> 0, 6 notes/bar -> 1.
            const double dn = (static_cast<double>(t.beats.size()) - 3.0) / 3.0;
            // Match is highest when the template's density tracks Energy; cubed
            // to sharpen the bias (energetic sessions clearly favour busier
            // grooves), with a small floor so nothing is ever impossible.
            const double match = e * dn + (1.0 - e) * (1.0 - dn);
            weights.push_back(0.04 + match * match * match);
        }
        std::discrete_distribution<std::size_t> pick(weights.begin(), weights.end());
        rhythmTemplate_ = kTemplates[pick(rng_)].beats;
        rhythmCursor_ = 0;
    }

    // The pitch class (semitones above the root, mod octave) of a scale degree.
    int pitchClassOf(int degree) const {
        const int step = wrap(degree, degreesPerOctave_);
        return scale_.intervals[static_cast<std::size_t>(step)] % 12;
    }

    // The degree in [0, totalDegrees) nearest to `from` whose pitch class is the
    // tonic (0) or, if `wantFifth`, also a perfect fifth (7). Falls back to
    // `from` if nothing matches (shouldn't happen for real scales).
    int nearestDegreeWithPitchClass(int from, bool wantFifth) const {
        int best = from;
        int bestDist = std::numeric_limits<int>::max();
        for (int d = 0; d < totalDegrees_; ++d) {
            const int pc = pitchClassOf(d);
            if (pc != 0 && !(wantFifth && pc == 7)) continue;
            const int dist = std::abs(d - from);
            if (dist < bestDist) {
                bestDist = dist;
                best = d;
            }
        }
        return best;
    }

    const BrightnessGrid& grid_;
    const Scale& scale_;
    const MelodyOptions& options_;
    MelodyChain& chain_;
    int totalDegrees_;
    int degreesPerOctave_;
    std::mt19937& rng_;
    float bias_;
    int col_;
    int row_;
    std::size_t degree_;
    double localBeat_ = 0.0;  // beats elapsed since the current phrase began
    int chordPcs_[3] = {0, 4, 7};  // current-chord pitch classes (strong-beat targets)
    int curChordRoot_ = 0;         // diagnostics only (bug-4 hook): last targeted root degree
    // Harmonic backbone shared with the arp/chords: a session-locked progression
    // the melody outlines, one chord per bar, tracked by a running beat count.
    std::vector<int> progression_;
    int tonicPc_ = 0;
    bool progMajor_ = false;
    double beatsPerBar_ = 4.0;
    double harmonyBeat_ = 0.0;  // beats elapsed across the whole melody
    // Session-locked rhythm: one one-bar groove the melody cycles through.
    std::vector<double> rhythmTemplate_;
    std::size_t rhythmCursor_ = 0;
};

// Builds a structured, phrased melody: motif (A), varied repeat (A'),
// contrasting phrase (B), extended as needed (A'' B ...), then a tonic-landing
// closing phrase. Rests fall between phrases at kRestProbability, and each
// phrase may sprout an arpeggio ornament.
Melody generatePhrased(const BrightnessGrid& grid, const Scale& scale,
                       const MelodyOptions& options, MelodyChain& chain,
                       int totalDegrees, std::mt19937& rng) {
    Melody melody;

    PhraseBuilder builder(grid, scale, options, chain, totalDegrees, rng);

    // Motif length: 3-5 notes.
    std::uniform_int_distribution<int> motifLenDist(3, 5);
    const int motifLen = motifLenDist(rng);

    const int target =
        options.length > 0 ? options.length : static_cast<int>(grid.cellCount());

    // Phrase 0 is the motif A (a smooth walk with a tonic/fifth-leaning end).
    std::vector<std::vector<PhraseNote>> phrases;
    const std::vector<PhraseNote> motif =
        builder.walkPhrase(motifLen, /*newRegion=*/false,
                           /*tonicFifthEnding=*/true);
    phrases.push_back(motif);

    // Body: alternate variations of the motif (odd positions -> A', A'', ...)
    // with fresh contrasting phrases (even positions -> B). Always emit at least
    // A, A' and B before closing; then keep going until we reach the target,
    // leaving room for the closing phrase.
    int bodyNotes = static_cast<int>(motif.size());
    int position = 1;
    std::uniform_real_distribution<double> repeatCoin(0.0, 1.0);
    while (static_cast<int>(phrases.size()) < 3 ||
           bodyNotes < target - motifLen) {
        std::vector<PhraseNote> phrase;
        if (position % 2 == 1) {
            // Variation slot: with probability `repetition` repeat the motif
            // verbatim, otherwise emit a transposed A' variation. High
            // Repetition -> the hook recurs; low Repetition -> it keeps evolving.
            phrase = repeatCoin(rng) < options.repetition
                         ? motif
                         : builder.varyMotif(motif);  // A', A'', ...
            // Diagnostics only (bug-4 hook): these notes are copied/transposed,
            // not snapped in their new bar, so clear the inherited snap flags.
            // Purely a debug-field reset — pitches/timing/RNG untouched. Indexed
            // (not range-for) to avoid a GCC-15 -Wfree-nonheap-object false
            // positive from inlining varyMotif's returned vector.
            for (std::size_t k = 0; k < phrase.size(); ++k) {
                phrase[k].dbgStrong = false;
                phrase[k].dbgSnapped = false;
                phrase[k].dbgChordRoot = -1;
                phrase[k].eligible = false;  // copied notes are never re-harmonised
                phrase[k].snapCoin = 1.0;
            }
        } else {
            phrase = builder.walkPhrase(motifLen, /*newRegion=*/true,
                                        /*tonicFifthEnding=*/true);  // B
        }
        bodyNotes += static_cast<int>(phrase.size());
        phrases.push_back(std::move(phrase));
        ++position;
        if (position > 4096) break;  // runaway guard
    }

    // Closing phrase: cadence onto the tonic.
    phrases.push_back(builder.closingPhrase(motifLen));

    // Ornament each phrase (probabilistically), then flatten into timed notes,
    // inserting rests at phrase boundaries.
    double beat = 0.0;
    // Pass-2 inputs, parallel with melody.notes: the recorded snap coin and
    // whether the note is a walked snap-site candidate (bug 4, 4a two-pass).
    std::vector<double> snapCoins;
    std::vector<char> eligible;
    for (std::size_t p = 0; p < phrases.size(); ++p) {
        const bool finalPhrase = p + 1 == phrases.size();
        // Ornament every phrase but the closing one — the cadence's final tonic
        // must survive intact.
        if (!finalPhrase) {
            builder.maybeOrnament(phrases[p]);
        }

        // Shape the phrase's dynamics (after any ornament, so the added figure
        // notes are shaped too); the last phrase decrescendos.
        builder.applyPhraseDynamics(phrases[p], finalPhrase);

        if (p > 0 && builder.rollRest()) {
            // Higher Energy tightens the gaps between phrases (0 -> ~1.3x rests,
            // 1 -> ~0.7x), for a more driving feel.
            beat += builder.restLength() * (1.3 - 0.6 * clampUnit(options.energy));
        }

        melody.phraseStarts.push_back(melody.notes.size());
        for (const PhraseNote& pn : phrases[p]) {
            Note note;
            note.noteNumber = pn.noteNumber;
            note.velocity = applyEnergy(pn.velocity, options.energy);
            note.startBeats = beat;
            note.lengthBeats = pn.lengthBeats;
            melody.notes.push_back(note);
            melody.degrees.push_back(pn.degree);
            melody.cells.push_back(GridCell{pn.col, pn.row});
            melody.dbgStrong.push_back(pn.dbgStrong ? 1 : 0);       // diagnostics only
            melody.dbgSnapped.push_back(pn.dbgSnapped ? 1 : 0);     // diagnostics only
            melody.dbgChordRoot.push_back(pn.dbgChordRoot);         // diagnostics only
            snapCoins.push_back(pn.snapCoin);
            eligible.push_back(pn.eligible ? 1 : 0);
            beat += pn.lengthBeats;
        }
    }

    // Pass 2 (bug 4, 4a): now that every note has its REAL emitted start beat,
    // re-decide the strong beat and chord-tone snap against that beat instead of
    // the generation-time clock. Draws no rng (reuses the recorded snap coin), so
    // it changes only pitches — timing/rests/RNG stream are untouched.
    builder.reharmonizeAgainstRealBeats(melody, snapCoins, eligible);

    return melody;
}

// ---- arpeggiator ------------------------------------------------------------

// One full cycle of chord-tone indices in the requested pattern order. Random
// is handled per-step by the caller, so here it just returns the ascending set.
std::vector<int> orderChord(const std::vector<int>& asc, ArpPattern pattern) {
    const int n = static_cast<int>(asc.size());
    std::vector<int> seq;
    switch (pattern) {
        case ArpPattern::Down:
            seq.assign(asc.rbegin(), asc.rend());
            break;
        case ArpPattern::UpDown:
            seq = asc;  // ascending
            for (int i = n - 2; i >= 1; --i) {
                seq.push_back(asc[static_cast<std::size_t>(i)]);  // ...and back
            }
            break;
        case ArpPattern::Converge: {
            int lo = 0, hi = n - 1;
            bool takeLo = true;
            while (lo <= hi) {
                seq.push_back(takeLo ? asc[static_cast<std::size_t>(lo++)]
                                     : asc[static_cast<std::size_t>(hi--)]);
                takeLo = !takeLo;
            }
            break;
        }
        case ArpPattern::Up:
        case ArpPattern::Random:
        default:
            seq = asc;
            break;
    }
    if (seq.empty()) seq.push_back(0);
    return seq;
}

// ---- anti-boring post-processing --------------------------------------------

// A safety net enforcing the "don't be static" criteria on a finished melody:
// breaks up any pitch that dominates the line and guarantees a minimum spread of
// scale tones when Energy/Complexity ask for activity. Only pitches move (by
// whole scale degrees); timing is untouched, so it composes with Lock Rhythm.
// Deterministic given `rng`.
void enforceVariety(Melody& melody, const Scale& scale,
                    const MelodyOptions& options, std::mt19937& rng) {
    const std::size_t n = melody.notes.size();
    if (n < 6 || melody.degrees.size() != n) return;

    const double repetition = clampUnit(options.repetition);
    const bool wantVariety =
        clampUnit(options.energy) > 0.4 || clampUnit(options.arpeggioAmount) > 0.4;
    const int span = options.octaveSpan;
    const int usable = scale.usableDegrees(span);
    if (usable < 4) return;  // pentatonic-in-one-octave etc.: leave it be

    std::uniform_int_distribution<int> shiftPick(0, 1);
    auto reNote = [&](std::size_t i, int newDegree) {
        newDegree = newDegree < 0 ? 0 : (newDegree >= usable ? usable - 1 : newDegree);
        melody.degrees[i] = newDegree;
        melody.notes[i].noteNumber = scale.noteAt(newDegree, span);
    };

    // 1) Pitch dominance: if one pitch is used for > 60% of notes (and Repetition
    //    isn't near max), shift alternate offending notes to a neighbour degree.
    {
        std::unordered_map<int, int> counts;
        for (const Note& nt : melody.notes) counts[nt.noteNumber]++;
        int domNote = melody.notes[0].noteNumber, domCount = 0;
        for (const auto& kv : counts)
            if (kv.second > domCount) { domCount = kv.second; domNote = kv.first; }

        if (repetition < 0.8 &&
            static_cast<double>(domCount) > 0.6 * static_cast<double>(n)) {
            int seen = 0;
            for (std::size_t i = 0; i < n; ++i) {
                if (melody.notes[i].noteNumber != domNote) continue;
                // Keep ~half of them; move the rest to a neighbour scale degree.
                if ((seen++ % 2) == 1) {
                    const int dir = shiftPick(rng) ? 1 : -1;
                    reNote(i, melody.degrees[i] + dir * (1 + shiftPick(rng)));
                }
            }
        }
    }

    // 2) Minimum spread: when activity is wanted, ensure at least 4 distinct
    //    scale tones by nudging a few notes onto fresh degrees.
    if (wantVariety) {
        auto distinct = [&] {
            std::set<int> s;
            for (const Note& nt : melody.notes) s.insert(nt.noteNumber);
            return s.size();
        };
        for (int guard = 0; distinct() < 4 && guard < 8; ++guard) {
            const std::size_t i = static_cast<std::size_t>(rng() % n);
            reNote(i, melody.degrees[i] + (shiftPick(rng) ? 2 : -2));
        }
    }
}



// A scale is treated as major-flavoured when it has a major third above the
// tonic and no minor third (Ionian/Lydian/Mixolydian/major pentatonic);
// everything else (Aeolian/Dorian/Phrygian/blues/minor pentatonic) is minor.
// This lets Chords/Arp build real triads in the key even when the melodic scale
// is pentatonic or blues (where stacking raw scale degrees would give clusters).
bool scaleIsMajor(const Scale& scale) {
    bool hasMajor3 = false, hasMinor3 = false;
    for (int iv : scale.intervals) {
        const int pc = ((iv % 12) + 12) % 12;
        if (pc == 4) hasMajor3 = true;
        if (pc == 3) hasMinor3 = true;
    }
    return hasMajor3 && ! hasMinor3;
}

// MIDI notes of a diatonic chord: `size` tones stacked in thirds on diatonic
// degree `deg` of the key (0..6), voiced upward from `baseMidi + tonicPc`.
// For a full 7-degree scale the thirds are stacked from the DETECTED scale's own
// intervals, so modal / harmonic-minor colour is preserved (e.g. harmonic
// minor's raised 7th makes the V a real major triad with its leading tone).
// Scales without 7 degrees (pentatonics, the 6-note blues) have no diatonic
// 7-note set to stack real thirds from, so they keep the major/minor step-table
// fallback chosen by scaleIsMajor() — always a real triad regardless.
std::vector<int> diatonicChord(const Scale& scale, int tonicPc, bool major,
                               int deg, int size, int baseMidi) {
    const int* steps = (scale.degreesPerOctave() == 7)
                           ? scale.intervals.data()
                           : (major ? kMajorSteps : kMinorSteps);
    std::vector<int> notes;
    notes.reserve(static_cast<std::size_t>(std::max(1, size)));
    for (int k = 0; k < size; ++k) {
        const int d = deg + 2 * k;             // stacked scale-thirds
        const int idx = ((d % 7) + 7) % 7;
        const int oct = (d - idx) / 7;
        int n = baseMidi + tonicPc + steps[idx] + 12 * oct;
        notes.push_back(n < 0 ? 0 : (n > 127 ? 127 : n));
    }
    return notes;
}

// The chord-tone role (0 = root, 1 = third, 2 = fifth, ...) of a voiced MIDI
// note, found by matching its pitch class against the chord's tones. A triad's
// tones are distinct pitch classes, so this is robust to inversion and octave
// shifts (voice-leading and the arp's octave jumps only move notes by octaves).
// Falls back to 0 if nothing matches (should not happen for a real chord tone).
int chordToneRole(int note, const std::vector<int>& chordPitches) {
    const int pc = ((note % 12) + 12) % 12;
    for (std::size_t k = 0; k < chordPitches.size(); ++k) {
        if ((((chordPitches[k] % 12) + 12) % 12) == pc) {
            return static_cast<int>(k);
        }
    }
    return 0;
}

// Named diatonic pop progressions over the I/IV/V/vi family (scale degrees:
// 0 = I, 3 = IV, 4 = V, 5 = vi). Each is a self-contained four-chord loop that
// resolves when tiled, so no separate V–I forcing is needed. Shared by the
// melody's harmonic targeting, the arpeggiator, and the block-chord generator.
struct ProgressionTemplate {
    const char* name;
    int degrees[4];  // one chord per bar
};

const std::vector<ProgressionTemplate>& progressionTemplates() {
    static const std::vector<ProgressionTemplate> kTemplates = {
        {"I-V-vi-IV", {0, 4, 5, 3}},   // the "four-chord song"
        {"vi-IV-I-V", {5, 3, 0, 4}},   // relative-minor pop loop
        {"I-vi-IV-V", {0, 5, 3, 4}},   // 50s doo-wop
        {"I-IV-vi-V", {0, 3, 5, 4}},
        {"I-IV-V-vi", {0, 3, 4, 5}},   // rising, deceptive turnaround
        {"vi-IV-V-I", {5, 3, 4, 0}},   // resolves home on the downbeat
    };
    return kTemplates;
}

// Picks one progression for the whole session and tiles it to `count` chords.
// Session-locked: a fixed seed selects the same template every time, so the
// harmony has a stable identity rather than a fresh random walk each bar.
std::vector<int> progressionRoots(int count, std::mt19937& rng) {
    const auto& templates = progressionTemplates();
    std::uniform_int_distribution<std::size_t> pick(0, templates.size() - 1);
    const int* prog = templates[pick(rng)].degrees;
    const int n = std::max(1, count);
    std::vector<int> roots;
    roots.reserve(static_cast<std::size_t>(n));
    for (int c = 0; c < n; ++c) {
        roots.push_back(prog[c % 4]);
    }
    return roots;
}

// Register the chords/arp are voiced from. MUST be a multiple of 12 so that
// note%12 == (tonicPc + scaleStep)%12 and the harmony stays in the detected key.
constexpr int kHarmonyBaseMidi = 48;  // C3 — clear of the low-end mud

// An arpeggio that moves through a chord progression: each bar takes the next
// chord from a diatonic walk and spells it as a broken chord in `arpPattern`
// across `arpOctaves`. The grid is still walked so each note's velocity tracks
// image brightness and the Lens overlay lights up. This is a real arpeggio over
// changing harmony, not a static tonic triad run up and down.
Melody generateArpeggiated(const BrightnessGrid& grid, const Scale& scale,
                           const MelodyOptions& options, std::mt19937& rng) {
    Melody melody;

    const int degreesPerOctave = static_cast<int>(scale.degreesPerOctave());
    if (degreesPerOctave == 0) return melody;

    const int arpOctaves = std::max(1, options.arpOctaves);
    const int tonicPc = ((scale.rootNote % 12) + 12) % 12;
    const bool major = scaleIsMajor(scale);
    const bool randomPattern = options.arpPattern == ArpPattern::Random;

    const double rate = options.arpRate > 0.0 ? options.arpRate : 0.5;
    const double bpb = options.beatsPerBar > 0.0 ? options.beatsPerBar : 4.0;
    const int notesPerBar = std::max(1, static_cast<int>(std::lround(bpb / rate)));

    int length =
        options.length > 0 ? options.length : static_cast<int>(grid.cellCount());
    int bars;
    if (options.loopBars > 0) {
        bars = options.loopBars;
        length = bars * notesPerBar;  // fill exactly, for a seamless loop
    } else {
        bars = std::max(1, (length + notesPerBar - 1) / notesPerBar);
    }

    // One chord per bar.
    const std::vector<int> roots = progressionRoots(bars, rng);

    const int cols = grid.columns();
    const int rows = grid.rows();
    int col = cols / 2;
    int row = rows / 2;
    std::uniform_int_distribution<int> colDist(0, cols - 1);
    std::uniform_int_distribution<int> rowDist(0, rows - 1);
    std::uniform_int_distribution<int> stepDist(0, 7);

    melody.notes.reserve(static_cast<std::size_t>(length));
    melody.degrees.reserve(static_cast<std::size_t>(length));
    melody.chordTones.reserve(static_cast<std::size_t>(length));
    melody.cells.reserve(static_cast<std::size_t>(length));

    int currentBar = -1;
    std::vector<int> cycle;     // this bar's chord tones, ordered by pattern
    std::vector<int> barChord;  // this bar's canonical chord tones, for role lookup
    double beat = 0.0;
    for (int i = 0; i < length; ++i) {
        if (options.cellPath == CellPath::PureRandom) {
            col = colDist(rng);
            row = rowDist(rng);
        } else if (i > 0) {
            const int k = stepDist(rng);
            col = wrap(col + kDx[k], cols);
            row = wrap(row + kDy[k], rows);
        }
        const float brightness = grid.valueAt(col, row);

        const int bar = std::min(i / notesPerBar, bars - 1);
        if (bar != currentBar) {
            currentBar = bar;
            std::vector<int> asc;
            asc.reserve(static_cast<std::size_t>(3 * arpOctaves));
            for (int o = 0; o < arpOctaves; ++o)
                for (int n : diatonicChord(scale, tonicPc, major, roots[bar], 3,
                                           kHarmonyBaseMidi + 12 * o))
                    asc.push_back(n);
            cycle = orderChord(asc, options.arpPattern);
            barChord = diatonicChord(scale, tonicPc, major, roots[bar], 3,
                                     kHarmonyBaseMidi);
        }

        const int within = i % notesPerBar;
        int arpNote;
        if (randomPattern) {
            std::uniform_int_distribution<std::size_t> pick(0, cycle.size() - 1);
            arpNote = cycle[pick(rng)];
        } else {
            arpNote = cycle[static_cast<std::size_t>(within) % cycle.size()];
        }
        // Image-driven octave jump on very bright cells: contour from the image,
        // not a flat ladder.
        if (brightness > 0.82f && arpNote + 12 <= 127) arpNote += 12;

        // Accents: bar downbeat hardest, on-beats accented, off-beats softer,
        // brighter cells hit harder — never flat velocity.
        const bool onBeat = (within % 2 == 0);
        const bool downBeat = (within == 0);
        int vel = brightnessToVelocity(brightness)
                  + (downBeat ? 22 : (onBeat ? 12 : -6))
                  + static_cast<int>(brightness * 10.0f);
        vel = applyEnergy(vel, options.energy);
        vel = vel < 1 ? 1 : (vel > 127 ? 127 : vel);

        // Swing (delay + shorten off-beats, lengthen on-beats so each pair still
        // spans 2*rate) and a gate so notes articulate rather than run together.
        const double swing = 0.2;
        const double gate = 0.9;
        double start = beat;
        double len = rate;
        if (! randomPattern) {
            if (onBeat) len = rate * (1.0 + swing);
            else { start = beat + rate * swing; len = rate * (1.0 - swing); }
        }

        Note note;
        note.noteNumber = arpNote;
        note.velocity = vel;
        note.startBeats = start;
        note.lengthBeats = len * gate;

        // Degree metadata: arp notes are chord tones, not melodic-scale degrees,
        // so `degrees` is the -1 sentinel and the chord-tone role (robust to the
        // octave jump above and the pattern ordering) carries the identity. No
        // pitch is touched — metadata only.
        const int role = chordToneRole(arpNote, barChord);
        melody.notes.push_back(note);
        melody.degrees.push_back(-1);
        melody.chordTones.push_back(role);
        melody.cells.push_back(GridCell{col, row});
        beat += rate;
    }
    return melody;
}

// ---- chords -----------------------------------------------------------------

// A chord progression: a session-locked named pop template (I/IV/V/vi), each
// chord voiced by inversion for smooth voice-leading (the top voice moves as
// little as possible), one chord per bar — two per bar when Energy is high.
// Brightness and Energy set velocity and accent the downbeat of each bar.
Melody generateChords(const BrightnessGrid& grid, const Scale& scale,
                      const MelodyOptions& options, std::mt19937& rng) {
    Melody melody;

    const int size = std::max(2, options.chordSize);
    const int tonicPc = ((scale.rootNote % 12) + 12) % 12;
    const bool major = scaleIsMajor(scale);
    const double energy = clampUnit(options.energy);
    const double bpb = options.beatsPerBar > 0.0 ? options.beatsPerBar : 4.0;

    // Harmonic rhythm: one chord per bar, two per bar when energetic.
    const int chordsPerBar = energy > 0.55 ? 2 : 1;
    const double rate = bpb / chordsPerBar;

    int chords =
        options.length > 0 ? options.length : static_cast<int>(grid.cellCount());
    if (options.loopBars > 0) chords = options.loopBars * chordsPerBar;
    if (chords < 1) chords = 1;

    const int cols = grid.columns();
    const int rows = grid.rows();
    int col = cols / 2;
    int row = rows / 2;
    std::uniform_int_distribution<int> stepDist(0, 7);

    // Progression, then impose a V–I cadence at the end so the loop resolves.
    // A named pop progression, session-locked and tiled to fill the bars. The
    // template is a self-resolving loop, so no separate V–I is imposed here.
    std::vector<int> roots = progressionRoots(chords, rng);

    // Smooth voice-leading: choose the inversion whose top note is closest to the
    // previous chord's top note.
    auto voiceLead = [] (std::vector<int> notes, int prevTop) {
        if (prevTop < 0 || notes.size() < 2) return notes;
        std::vector<int> best = notes, cur = notes;
        int bestDist = std::numeric_limits<int>::max();
        for (std::size_t inv = 0; inv < notes.size(); ++inv) {
            const int top = *std::max_element(cur.begin(), cur.end());
            const int dist = std::abs(top - prevTop);
            if (dist < bestDist) { bestDist = dist; best = cur; }
            *std::min_element(cur.begin(), cur.end()) += 12;  // next inversion
        }
        std::sort(best.begin(), best.end());
        return best;
    };

    int prevTop = -1;
    double beat = 0.0;
    for (int c = 0; c < chords; ++c) {
        if (options.cellPath == CellPath::PureRandom) {
            std::uniform_int_distribution<int> cd(0, cols - 1), rd(0, rows - 1);
            col = cd(rng);
            row = rd(rng);
        } else if (c > 0) {
            const int k = stepDist(rng);
            col = wrap(col + kDx[k], cols);
            row = wrap(row + kDy[k], rows);
        }
        const float brightness = grid.valueAt(col, row);

        // Presence + a downbeat accent (first chord of each bar).
        const bool downBeat = (c % chordsPerBar == 0);
        int velocity = static_cast<int>(std::lround(72.0 + 30.0 * clampUnit(brightness)))
                       + (downBeat ? 8 : 0);
        velocity = applyEnergy(velocity, options.energy);

        // Canonical (stacked) chord tones, kept for chord-tone role lookup: the
        // voice-led copy sorts by pitch, so `v` no longer identifies the root/
        // third/fifth once an inversion is chosen.
        const std::vector<int> chordPitches =
            diatonicChord(scale, tonicPc, major, roots[c], size, kHarmonyBaseMidi);
        std::vector<int> notes = voiceLead(chordPitches, prevTop);
        prevTop = *std::max_element(notes.begin(), notes.end());

        for (std::size_t v = 0; v < notes.size(); ++v) {
            Note note;
            note.noteNumber = notes[v] < 0 ? 0 : (notes[v] > 127 ? 127 : notes[v]);
            note.velocity = (v + 1 == notes.size())
                                ? std::min(127, velocity + 6)  // top voice sings
                                : velocity;
            note.startBeats = beat;
            note.lengthBeats = rate;
            // Degree metadata: chord notes are not melodic-scale degrees, so
            // `degrees` is the -1 sentinel; the chord-tone role (by pitch class,
            // correct after inversion) carries the identity. Metadata only; the
            // voiced pitch above is unchanged.
            const int role = chordToneRole(notes[v], chordPitches);
            melody.notes.push_back(note);
            melody.degrees.push_back(-1);
            melody.chordTones.push_back(role);
            melody.cells.push_back(GridCell{col, row});
        }
        beat += rate;
    }
    return melody;
}

// ---- loop post-processing ---------------------------------------------------

// Snaps the material's total length up to a whole number of bars by extending
// the note(s) that end last, so a looping player repeats it seamlessly. When
// `loopBars` > 0 it targets at least that many bars; otherwise it rounds up to
// the next whole bar. A no-op when the material already fills whole bars (as
// the arpeggiator/chords loop paths arrange).
void padToWholeBars(Melody& melody, double beatsPerBar, int loopBars) {
    if (melody.notes.empty() || beatsPerBar <= 0.0) return;

    double total = 0.0;
    for (const Note& n : melody.notes) {
        total = std::max(total, n.startBeats + n.lengthBeats);
    }

    const double ceilBars = std::ceil(total / beatsPerBar - 1e-9);
    const double bars = std::max({1.0, ceilBars, static_cast<double>(loopBars)});
    const double target = bars * beatsPerBar;
    if (target > total + 1e-9) {
        // Extend every note that currently ends at the max (chords end together)
        // so a final block chord stays intact.
        for (Note& n : melody.notes) {
            if (n.startBeats + n.lengthBeats > total - 1e-9) {
                n.lengthBeats += (target - total);
            }
        }
    }
}

}  // namespace

Melody generateMelody(const BrightnessGrid& grid, const Scale& scale,
                      const MelodyOptions& options, std::mt19937& rng) {
    Melody melody;

    const TheoryWeights weights;
    const std::size_t degreesPerOctave = scale.degreesPerOctave();
    const int totalDegrees = scale.usableDegrees(options.octaveSpan);
    const int cols = grid.columns();
    const int rows = grid.rows();

    if (totalDegrees <= 0 || degreesPerOctave == 0 || cols <= 0 || rows <= 0) {
        return melody;
    }

    // Arpeggio and Chords modes supersede the phrase/freeform Markov walk.
    if (options.mode == GenerationMode::Arpeggio) {
        Melody arp = generateArpeggiated(grid, scale, options, rng);
        if (options.loopBars > 0)
            padToWholeBars(arp, options.beatsPerBar, options.loopBars);
        return arp;
    }
    if (options.mode == GenerationMode::Chords) {
        Melody chords = generateChords(grid, scale, options, rng);
        if (options.loopBars > 0)
            padToWholeBars(chords, options.beatsPerBar, options.loopBars);
        return chords;
    }

    const TransitionMatrix matrix = TransitionMatrix::fromTheory(
        static_cast<std::size_t>(totalDegrees), degreesPerOctave, weights);
    MelodyChain chain(matrix, rng, weights);

    if (options.phraseMode == PhraseMode::Freeform) {
        melody = generateFreeform(grid, scale, options, chain, totalDegrees, rng);
    } else {
        melody = generatePhrased(grid, scale, options, chain, totalDegrees, rng);
    }

    // Enforce the anti-boring guarantees (no dominant pitch, enough spread).
    enforceVariety(melody, scale, options, rng);

    if (options.loopBars > 0) {
        padToWholeBars(melody, options.beatsPerBar, options.loopBars);
    }
    return melody;
}

// ---- lock-aware recombination + mutation ------------------------------------

namespace {
// The nearest in-scale note above/below a MIDI note for a given scale, so a
// mutated pitch stays diatonic. Searches the scale's own note set across a few
// octaves around the source.
int nearestScaleNote(const Scale& scale, int midi, int span) {
    const int usable = scale.usableDegrees(span);
    int best = midi;
    int bestDist = std::numeric_limits<int>::max();
    for (int d = 0; d < usable; ++d) {
        const int n = scale.noteAt(d, span);
        const int dist = std::abs(n - midi);
        if (dist < bestDist) {
            bestDist = dist;
            best = n;
        }
    }
    return best;
}
}  // namespace

Melody recombineLocked(const Melody& previous, const Melody& candidate,
                       const scales::Scale& scale, RegenLocks locks,
                       const MelodyOptions& options) {
    // Fast paths: nothing locked -> the fresh candidate; both locked -> the
    // previous melody unchanged.
    if (!locks.rhythm && !locks.pitch) return candidate;
    if (locks.rhythm && locks.pitch) return previous;
    if (previous.notes.empty()) return candidate;
    if (candidate.notes.empty()) return previous;

    // The timing track (start/length) comes from the rhythm source; the pitch
    // track (note/degree/velocity/cell) from the pitch source.
    const Melody& rhythmSrc = locks.rhythm ? previous : candidate;
    const Melody& pitchSrc = locks.pitch ? previous : candidate;
    const int span = std::max(options.octaveSpan, 2);

    Melody out;
    const std::size_t n = rhythmSrc.notes.size();
    out.notes.reserve(n);
    out.degrees.reserve(n);
    out.cells.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t pi = i % pitchSrc.notes.size();  // cycle pitches to fill
        Note note;
        note.startBeats = rhythmSrc.notes[i].startBeats;
        note.lengthBeats = rhythmSrc.notes[i].lengthBeats;
        note.noteNumber = pitchSrc.notes[pi].noteNumber;
        note.velocity = pitchSrc.notes[pi].velocity;
        out.notes.push_back(note);
        out.degrees.push_back(pi < pitchSrc.degrees.size()
                                  ? pitchSrc.degrees[pi]
                                  : 0);
        out.cells.push_back(pi < pitchSrc.cells.size() ? pitchSrc.cells[pi]
                                                       : GridCell{});
    }
    (void)scale;
    (void)span;
    // Phrase boundaries follow the rhythm track (that's what defines timing).
    out.phraseStarts = rhythmSrc.phraseStarts;
    return out;
}

Melody mutate(const Melody& base, const scales::Scale& scale, RegenLocks locks,
              double amount, const MelodyOptions& options, std::mt19937& rng) {
    Melody out = base;
    if (out.notes.empty()) return out;
    if (locks.rhythm && locks.pitch) return out;  // nothing free to change

    const double amt = clampUnit(amount);
    const int span = std::max(options.octaveSpan, 2);
    std::uniform_real_distribution<double> coin(0.0, 1.0);
    std::uniform_int_distribution<int> stepPick(0, 3);  // +/-1 or +/-2 scale steps
    const double durations[3] = {0.5, 1.0, 2.0};
    std::uniform_int_distribution<int> durPick(0, 2);

    for (std::size_t i = 0; i < out.notes.size(); ++i) {
        if (coin(rng) >= amt) continue;  // leave this note alone

        // Pitch: nudge to a neighbouring scale degree (kept diatonic), unless
        // pitch is locked.
        if (!locks.pitch) {
            const int deltaSteps = (stepPick(rng) < 2 ? 1 : 2) *
                                   (coin(rng) < 0.5 ? -1 : 1);
            // Approximate a scale-step move in semitones, then snap to scale.
            const int approx = out.notes[i].noteNumber + deltaSteps * 2;
            out.notes[i].noteNumber = nearestScaleNote(scale, approx, span);
        }

        // Rhythm: retime this note to a neighbouring length, unless locked. The
        // timeline is then re-laid so notes stay contiguous.
        if (!locks.rhythm) {
            out.notes[i].lengthBeats = durations[durPick(rng)];
        }
    }

    // Re-lay start times if rhythm was allowed to change (keeps notes gapless).
    if (!locks.rhythm) {
        double beat = out.notes.front().startBeats;
        for (Note& n : out.notes) {
            n.startBeats = beat;
            beat += n.lengthBeats;
        }
        if (options.loopBars > 0)
            padToWholeBars(out, options.beatsPerBar, options.loopBars);
    }
    return out;
}

}  // namespace lumena::melody
