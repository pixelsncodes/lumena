#include "melody/MelodyGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

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
        note.velocity = brightnessToVelocity(brightness);
        note.startBeats = beat;
        note.lengthBeats = (options.rhythm == RhythmMode::Flowing)
                               ? flowingDuration(brightness, rng)
                               : 1.0;

        melody.notes.push_back(note);
        melody.degrees.push_back(static_cast<int>(degree));
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
          degree_(static_cast<std::size_t>(totalDegrees / 2)) {}

    // Builds a phrase of `count` notes by walking the grid. When `newRegion` is
    // set, the walk teleports to a random cell first (the contrasting-phrase B).
    // When `tonicFifthEnding` is set, the last note is pulled hard toward the
    // nearest tonic or fifth degree.
    std::vector<PhraseNote> walkPhrase(int count, bool newRegion,
                                       bool tonicFifthEnding) {
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

private:
    // Advances one step: moves on the grid, samples the next degree (optionally
    // biased toward a phrase-ending tonic/fifth), and packages a PhraseNote.
    PhraseNote stepNote(bool jump, bool ending) {
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
        if (ending) {
            const int target = nearestDegreeWithPitchClass(
                static_cast<int>(degree_), /*wantFifth=*/true);
            degree_ = chain_.nextBiased(degree_, static_cast<float>(target),
                                        kPhraseEndBias);
        } else {
            const int target = mapBrightnessToDegree(brightness, totalDegrees_);
            degree_ = chain_.nextBiased(degree_, static_cast<float>(target),
                                        bias_);
        }

        PhraseNote note;
        note.degree = static_cast<int>(degree_);
        note.noteNumber = scale_.noteAt(note.degree, options_.octaveSpan);
        note.velocity = brightnessToVelocity(brightness);
        note.brightness = brightness;
        note.lengthBeats = (options_.rhythm == RhythmMode::Flowing)
                               ? flowingDuration(brightness, rng_)
                               : 1.0;
        return note;
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
    while (static_cast<int>(phrases.size()) < 3 ||
           bodyNotes < target - motifLen) {
        std::vector<PhraseNote> phrase;
        if (position % 2 == 1) {
            phrase = builder.varyMotif(motif);  // A', A'', ...
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
            beat += builder.restLength();
        }

        melody.phraseStarts.push_back(melody.notes.size());
        for (const PhraseNote& pn : phrases[p]) {
            Note note;
            note.noteNumber = pn.noteNumber;
            note.velocity = pn.velocity;
            note.startBeats = beat;
            note.lengthBeats = pn.lengthBeats;
            melody.notes.push_back(note);
            melody.degrees.push_back(pn.degree);
            beat += pn.lengthBeats;
        }
    }

    return melody;
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

    const TransitionMatrix matrix = TransitionMatrix::fromTheory(
        static_cast<std::size_t>(totalDegrees), degreesPerOctave, weights);
    MelodyChain chain(matrix, rng, weights);

    if (options.phraseMode == PhraseMode::Freeform) {
        return generateFreeform(grid, scale, options, chain, totalDegrees, rng);
    }
    return generatePhrased(grid, scale, options, chain, totalDegrees, rng);
}

}  // namespace lumena::melody
