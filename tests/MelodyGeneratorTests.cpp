// Unit tests for the high-level melody generator: melodic smoothness (spatial
// coherence via the grid random walk + low brightness bias), brightness-driven
// velocity written into the MIDI byte stream, and Flowing/Straight rhythm.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <set>
#include <vector>

#include "image/BrightnessGrid.h"
#include "image/Image.h"
#include "melody/MelodyGenerator.h"
#include "midi/MidiFileWriter.h"
#include "midi/MidiSequence.h"
#include "scales/Scale.h"
#include "scales/ScaleLibrary.h"
#include "test_util.h"

namespace {

using lumena::image::BrightnessGrid;
using lumena::image::Image;
using lumena::melody::ArpPattern;
using lumena::melody::CellPath;
using lumena::melody::generateMelody;
using lumena::melody::kMaxVelocity;
using lumena::melody::kMinVelocity;
using lumena::melody::Melody;
using lumena::melody::MelodyOptions;
using lumena::melody::mutate;
using lumena::melody::PhraseMode;
using lumena::melody::recombineLocked;
using lumena::melody::RegenLocks;
using lumena::melody::RhythmMode;
using lumena::midi::MidiFileWriter;
using lumena::midi::MidiSequence;
using lumena::midi::Note;
using lumena::scales::mapBrightnessToDegree;
using lumena::scales::Scale;

// A smooth 2-D brightness ramp: brightness rises with both x and y, so any two
// adjacent grid cells differ only slightly. This is what makes the random walk
// yield gradually-changing targets rather than jumps.
Image makeDiagonalGradient(int width, int height) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int denom = (width - 1) + (height - 1);
            const int v = denom > 0 ? ((x + y) * 255) / denom : 0;
            const auto g = static_cast<std::uint8_t>(v);
            const std::size_t i =
                (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[i + 0] = g;
            pixels[i + 1] = g;
            pixels[i + 2] = g;
            pixels[i + 3] = 255;
        }
    }
    return Image(width, height, std::move(pixels));
}

// A uniformly bright image: every cell reads ~1.0, so arpeggio ornaments (which
// prefer cells brighter than 0.7) always have a valid trigger site.
Image makeBright(int width, int height) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4, 255);
    return Image(width, height, std::move(pixels));
}

// A per-grid-cell checkerboard: each `cellPx`-sized block flips between black
// and white, so with a matching grid resolution every cell's 8-neighbourhood
// spans the full 0..1 range — maximal local contrast, the density hook's cue to
// subdivide. (makeBright, by contrast, has ~zero local contrast everywhere.)
Image makeCheckerboard(int width, int height, int cellPx) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4, 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool on = (((x / cellPx) + (y / cellPx)) % 2) == 0;
            const auto g = static_cast<std::uint8_t>(on ? 255 : 0);
            const std::size_t i =
                (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[i + 0] = g;
            pixels[i + 1] = g;
            pixels[i + 2] = g;
            pixels[i + 3] = 255;
        }
    }
    return Image(width, height, std::move(pixels));
}

Scale minorPentatonic() {
    return Scale{"A Minor Pentatonic", 57, {0, 3, 5, 7, 10}};
}

// D Harmonic Minor: like natural minor but with a RAISED 7th (interval 11, a
// leading tone). Root D = MIDI 62 (pitch class 2); the leading tone is C#
// (pitch class (2 + 11) % 12 == 1). Chords/Arp must spell the V from the scale's
// own 7th, so the V is major (A-C#-E) and pitch class 1 is emitted. (Bug 5: the
// engine currently spells from kMinorSteps and drops the raised 7th.)
Scale dHarmonicMinor() {
    return Scale{"D Harmonic Minor", 62, {0, 2, 3, 5, 7, 8, 11}};
}

// A Blues (minor blues): minor pentatonic plus the ♭5 blue note (interval 6).
// Root A = MIDI 57 (pitch class 9). Its ♭7 is G (pitch class (9 + 10) % 12 == 7).
// Because it is a 6-note scale it drops through diatonicChord's non-7-degree
// fallback, which spells a plain minor triad and silently loses the ♭7. Phase 4a
// makes the arp voice a real minor-7th (A-C-E-G) so the blue ♭7 sounds, every
// tone staying inside the parent A-minor key.
Scale aBlues() {
    return Scale{"A Blues", 57, {0, 3, 5, 6, 7, 10}};
}

// A flat mid-grey image: every cell reads ~0.5, safely below the arp's
// brightness>0.82 octave-jump threshold, so the emitted pitches are exactly the
// arp cycle with no image-driven octave lifts — lets a test read the raw figure.
Image makeGrey(int width, int height) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4, 128);
    for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;  // alpha
    return Image(width, height, std::move(pixels));
}

// The scale-degree deltas within a phrase [begin, end) of the degree track.
// Transposing a motif preserves these, so two phrases sharing a delta signature
// are the same motif (possibly transposed).
std::vector<int> phraseDeltas(const std::vector<int>& degrees, std::size_t begin,
                              std::size_t end) {
    std::vector<int> deltas;
    for (std::size_t i = begin + 1; i < end; ++i) {
        deltas.push_back(degrees[i] - degrees[i - 1]);
    }
    return deltas;
}

// Walks the serialised MIDI file and collects every note-on velocity byte. The
// writer emits a full status byte per event (no running status) and only
// note-on (0x9n), note-off (0x8n) and meta (0xFF) events, so a straight walk
// reading a VLQ delta then one event at a time is sufficient.
std::vector<int> noteOnVelocities(const std::vector<std::uint8_t>& bytes) {
    std::vector<int> velocities;
    // Header is 14 bytes (MThd + len 6 + 6 body); MTrk header is 8 bytes.
    std::size_t p = 14 + 8;
    auto readVlq = [&]() {
        std::uint32_t value = 0;
        while (p < bytes.size()) {
            const std::uint8_t b = bytes[p++];
            value = (value << 7) | (b & 0x7F);
            if ((b & 0x80) == 0) break;
        }
        return value;
    };
    while (p < bytes.size()) {
        readVlq();  // delta time
        if (p >= bytes.size()) break;
        const std::uint8_t status = bytes[p++];
        if (status == 0xFF) {                 // meta event
            ++p;                              // meta type
            const std::uint32_t len = readVlq();
            p += len;                         // skip meta body
        } else if ((status & 0xF0) == 0x90) {  // note-on: note, velocity
            ++p;                              // note number
            velocities.push_back(bytes[p++]);
        } else if ((status & 0xF0) == 0x80) {  // note-off: note, velocity
            p += 2;
        } else {
            break;  // unexpected; stop rather than misparse
        }
    }
    return velocities;
}

// ---- melodic smoothness: >= 60% of consecutive intervals are <= 2 degrees ---

void test_random_walk_is_smooth() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions opts;
    opts.length = 32;
    opts.phraseMode = PhraseMode::Freeform;  // flat stream: no phrase structure
    opts.rhythm = RhythmMode::Flowing;
    opts.cellPath = CellPath::RandomWalk;  // default, made explicit
    opts.brightnessBias = 0.25;            // default, made explicit

    std::mt19937 rng(12345u);
    const Melody melody = generateMelody(grid, scale, opts, rng);

    CHECK(melody.degrees.size() == 32);
    CHECK(melody.notes.size() == melody.degrees.size());

    long stepwise = 0;
    long total = 0;
    for (std::size_t i = 1; i < melody.degrees.size(); ++i) {
        const int d = melody.degrees[i] - melody.degrees[i - 1];
        if (std::abs(d) <= 2) {
            ++stepwise;
        }
        ++total;
    }
    CHECK(total == 31);
    // Regression guard: the melody must move mostly by scale steps, not leaps.
    CHECK(stepwise * 100 >= total * 60);
}

// ---- brightness -> velocity, verified in the emitted MIDI bytes -------------

void test_velocity_mapping_in_bytes() {
    // Endpoints of the mapping.
    CHECK(lumena::melody::brightnessToVelocity(0.0f) == kMinVelocity);
    CHECK(lumena::melody::brightnessToVelocity(1.0f) == kMaxVelocity);
    CHECK(lumena::melody::brightnessToVelocity(-5.0f) == kMinVelocity);
    CHECK(lumena::melody::brightnessToVelocity(9.0f) == kMaxVelocity);

    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions opts;
    opts.length = 48;
    opts.phraseMode = PhraseMode::Freeform;  // exact one-note-per-step stream
    std::mt19937 rng(7u);
    const Melody melody = generateMelody(grid, scale, opts, rng);
    CHECK(!melody.notes.empty());

    const MidiSequence seq(melody.notes, 120.0, 480);
    const std::vector<std::uint8_t> bytes = MidiFileWriter::toBytes(seq);
    const std::vector<int> velocities = noteOnVelocities(bytes);

    // One note-on per generated note.
    CHECK(velocities.size() == melody.notes.size());

    bool inRange = true;
    std::set<int> distinct;
    for (int v : velocities) {
        if (v < kMinVelocity || v > kMaxVelocity) inRange = false;
        distinct.insert(v);
    }
    CHECK(inRange);
    // Not all uniform: a gradient image produces a spread of dynamics.
    CHECK(distinct.size() > 1);

    // And the bytes agree with the note velocities we handed the writer.
    bool matches = velocities.size() == melody.notes.size();
    for (std::size_t i = 0; i < velocities.size() && matches; ++i) {
        if (velocities[i] != melody.notes[i].velocity) matches = false;
    }
    CHECK(matches);
}

// ---- rhythm modes ----------------------------------------------------------

void test_rhythm_modes() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    // Straight: every note is a quarter (1.0 beat).
    {
        MelodyOptions opts;
        opts.length = 40;
        opts.phraseMode = PhraseMode::Freeform;
        opts.rhythm = RhythmMode::Straight;
        std::mt19937 rng(3u);
        const Melody melody = generateMelody(grid, scale, opts, rng);
        CHECK(melody.notes.size() == 40);
        bool allQuarter = true;
        for (const Note& n : melody.notes) {
            if (n.lengthBeats != 1.0) allQuarter = false;
        }
        CHECK(allQuarter);
    }

    // Flowing: durations vary among eighth/quarter/half.
    {
        MelodyOptions opts;
        opts.length = 64;
        opts.phraseMode = PhraseMode::Freeform;
        opts.rhythm = RhythmMode::Flowing;
        std::mt19937 rng(4u);
        const Melody melody = generateMelody(grid, scale, opts, rng);
        CHECK(melody.notes.size() == 64);
        std::set<double> lengths;
        bool onlyValidLengths = true;
        for (const Note& n : melody.notes) {
            lengths.insert(n.lengthBeats);
            if (n.lengthBeats != 0.5 && n.lengthBeats != 1.0 &&
                n.lengthBeats != 2.0) {
                onlyValidLengths = false;
            }
        }
        CHECK(onlyValidLengths);
        CHECK(lengths.size() > 1);  // not uniform quarter notes
    }
}

// ---- length and pure-random option -----------------------------------------

void test_length_and_pure_random() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);  // 192 cells
    const Scale scale = minorPentatonic();

    // length 0 -> one note per grid cell.
    {
        MelodyOptions opts;
        opts.length = 0;
        opts.phraseMode = PhraseMode::Freeform;
        std::mt19937 rng(1u);
        const Melody melody = generateMelody(grid, scale, opts, rng);
        CHECK(melody.notes.size() == grid.cellCount());
    }

    // Explicit length is honoured, and PureRandom stays available as an option.
    {
        MelodyOptions opts;
        opts.length = 24;
        opts.phraseMode = PhraseMode::Freeform;
        opts.cellPath = CellPath::PureRandom;
        std::mt19937 rng(2u);
        const Melody melody = generateMelody(grid, scale, opts, rng);
        CHECK(melody.notes.size() == 24);
    }
}

// ---- reproducibility with a fixed seed -------------------------------------

void test_reproducible() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions opts;
    opts.length = 50;
    std::mt19937 r1(99u);
    std::mt19937 r2(99u);
    const Melody a = generateMelody(grid, scale, opts, r1);
    const Melody b = generateMelody(grid, scale, opts, r2);
    CHECK(a.degrees == b.degrees);
    bool sameNotes = a.notes.size() == b.notes.size();
    for (std::size_t i = 0; i < a.notes.size() && sameNotes; ++i) {
        if (a.notes[i].noteNumber != b.notes[i].noteNumber ||
            a.notes[i].velocity != b.notes[i].velocity ||
            a.notes[i].lengthBeats != b.notes[i].lengthBeats) {
            sameNotes = false;
        }
    }
    CHECK(sameNotes);
}

// ---- phrased mode: a motif is repeated (as a transposed repeat) -------------

void test_phrased_repeats_motif() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions opts;
    opts.length = 32;
    opts.phraseMode = PhraseMode::Phrased;
    opts.arpeggioAmount = 0.0;  // keep phrase note counts exact for comparison

    std::mt19937 rng(2024u);
    const Melody melody = generateMelody(grid, scale, opts, rng);

    // Phrased mode records phrase boundaries; there must be at least A, A', B
    // and a closing phrase.
    CHECK(melody.phraseStarts.size() >= 4);
    CHECK(melody.notes.size() == melody.degrees.size());

    // The motif is phrase 0; the varied repeat A' is phrase 1. A' is a
    // transposition of A, so their scale-degree deltas match exactly.
    const std::vector<std::size_t>& starts = melody.phraseStarts;
    const std::vector<int> motif = phraseDeltas(melody.degrees, starts[0], starts[1]);
    CHECK(!motif.empty());

    bool foundRepeat = false;
    for (std::size_t p = 1; p + 1 < starts.size(); ++p) {
        const std::vector<int> other =
            phraseDeltas(melody.degrees, starts[p], starts[p + 1]);
        if (other == motif) {
            foundRepeat = true;
            break;
        }
    }
    CHECK(foundRepeat);
}

// ---- phrased mode: the final note is a tonic held for >= a half note --------

void test_phrased_cadence_on_tonic() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    // Try several seeds: the cadence must hold regardless of the random walk.
    for (unsigned seed = 1; seed <= 40; ++seed) {
        MelodyOptions opts;
        opts.length = 24;
        opts.phraseMode = PhraseMode::Phrased;

        std::mt19937 rng(seed);
        const Melody melody = generateMelody(grid, scale, opts, rng);
        CHECK(!melody.notes.empty());

        const Note& last = melody.notes.back();
        const int lastDegree = melody.degrees.back();

        // Tonic: a degree whose pitch class equals the root's (interval 0).
        const int step =
            ((lastDegree % static_cast<int>(scale.intervals.size())) +
             static_cast<int>(scale.intervals.size())) %
            static_cast<int>(scale.intervals.size());
        CHECK(scale.intervals[static_cast<std::size_t>(step)] % 12 == 0);
        // And its MIDI pitch is a whole number of octaves above the root.
        CHECK((last.noteNumber - scale.rootNote) % 12 == 0);
        // Held at least a half note.
        CHECK(last.lengthBeats >= 2.0);
    }
}

// ---- phrased mode: rests appear between phrases at ~60% over many seeds ------

void test_phrased_rests_between_phrases() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    long boundaries = 0;
    long rested = 0;
    for (unsigned seed = 1; seed <= 200; ++seed) {
        MelodyOptions opts;
        opts.length = 40;
        opts.phraseMode = PhraseMode::Phrased;
        opts.arpeggioAmount = 0.0;  // isolate the rest behaviour

        std::mt19937 rng(seed);
        const Melody melody = generateMelody(grid, scale, opts, rng);
        const std::vector<std::size_t>& starts = melody.phraseStarts;

        // A rest shows up as a gap between the end of one phrase's last note and
        // the start of the next phrase's first note.
        for (std::size_t p = 1; p < starts.size(); ++p) {
            const std::size_t firstOfPhrase = starts[p];
            const std::size_t lastOfPrev = firstOfPhrase - 1;
            const double prevEnd = melody.notes[lastOfPrev].startBeats +
                                   melody.notes[lastOfPrev].lengthBeats;
            const double gap = melody.notes[firstOfPhrase].startBeats - prevEnd;
            ++boundaries;
            if (gap > 1e-9) ++rested;
        }
    }

    CHECK(boundaries > 100);  // enough samples for the rate to converge
    // Expected ~60%; allow a generous statistical band.
    const double rate = static_cast<double>(rested) / static_cast<double>(boundaries);
    CHECK(rate >= 0.50);
    CHECK(rate <= 0.70);
}

// ---- phrased mode: arpeggio ornaments stay within the scale -----------------

void test_phrased_arpeggios_in_scale() {
    // A fully-bright image guarantees ornament trigger sites (cells > 0.7).
    const Image image = makeBright(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    // Pitch classes that belong to the scale.
    std::set<int> scalePcs;
    for (int iv : scale.intervals) {
        scalePcs.insert(((iv % 12) + 12) % 12);
    }

    bool sawArpeggio = false;
    for (unsigned seed = 1; seed <= 30; ++seed) {
        MelodyOptions opts;
        opts.length = 40;
        opts.phraseMode = PhraseMode::Phrased;
        opts.arpeggioAmount = 1.0;  // force an ornament in every phrase

        std::mt19937 rng(seed);
        const Melody melody = generateMelody(grid, scale, opts, rng);

        // Every emitted pitch — arpeggio notes included — must be in the scale.
        for (const Note& n : melody.notes) {
            const int pc = ((n.noteNumber - scale.rootNote) % 12 + 12) % 12;
            CHECK(scalePcs.count(pc) == 1);
        }

        // Detect an arpeggio figure: three consecutive degrees stepping by a
        // constant +2 or -2 (the 0-2-4 pentatonic outline) with short (eighth
        // or triplet) durations.
        for (std::size_t i = 2; i < melody.degrees.size(); ++i) {
            const int d1 = melody.degrees[i - 1] - melody.degrees[i - 2];
            const int d2 = melody.degrees[i] - melody.degrees[i - 1];
            const bool shaped = (d1 == 2 && d2 == 2) || (d1 == -2 && d2 == -2);
            const bool shortNotes = melody.notes[i - 2].lengthBeats < 0.6 &&
                                    melody.notes[i - 1].lengthBeats < 0.6 &&
                                    melody.notes[i].lengthBeats < 0.6;
            if (shaped && shortNotes) sawArpeggio = true;
        }
    }
    // With ornaments forced on and bright trigger sites, at least one figure
    // should have been emitted across all seeds.
    CHECK(sawArpeggio);
}

// ---- phrased mode: velocity peaks in the interior of every phrase -----------

void test_phrased_dynamics_peak_mid_phrase() {
    // A uniformly-bright image: brightness is constant, so the phrase contour
    // alone shapes velocity and its peak must land in the phrase interior — the
    // loudest note is never the phrase's first or last note.
    const Image image = makeBright(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    for (unsigned seed = 1; seed <= 40; ++seed) {
        MelodyOptions opts;
        opts.length = 40;
        opts.phraseMode = PhraseMode::Phrased;
        opts.arpeggioAmount = 0.0;  // keep phrase note counts simple

        std::mt19937 rng(seed);
        const Melody melody = generateMelody(grid, scale, opts, rng);
        const std::vector<std::size_t>& starts = melody.phraseStarts;
        CHECK(starts.size() >= 4);

        for (std::size_t p = 0; p < starts.size(); ++p) {
            const std::size_t begin = starts[p];
            const std::size_t end =
                (p + 1 < starts.size()) ? starts[p + 1] : melody.notes.size();
            CHECK(end - begin >= 3);

            int maxV = -1;
            std::size_t maxAt = begin;
            for (std::size_t i = begin; i < end; ++i) {
                if (melody.notes[i].velocity > maxV) {
                    maxV = melody.notes[i].velocity;
                    maxAt = i;
                }
            }
            // The loudest note is interior: neither first nor last of the phrase.
            CHECK(maxAt != begin);
            CHECK(maxAt != end - 1);
        }
    }
}

// ---- phrased mode: dynamics move smoothly, no per-beat whiplash -------------

// Within a phrase, consecutive notes' velocities must not stab between extremes:
// the generator low-passes the brightness tint and slew-caps the note-to-note
// change to kMaxDynStep. At Energy 0.5 the post-generation energy scale is
// exactly 1.0, so the emitted velocities carry the cap verbatim. (Jumps ACROSS a
// phrase boundary are a fresh dynamic gesture and deliberately not capped, so the
// check runs within each phrase, using phraseStarts.)
void test_phrased_dynamics_are_smooth() {
    constexpr int kMaxDynStep = 12;  // must match MelodyGenerator.cpp
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    long maxWithinPhraseJump = 0;
    int lo = 200, hi = 0;
    for (unsigned seed = 1; seed <= 80; ++seed) {
        MelodyOptions opts;
        opts.length = 40;
        opts.phraseMode = PhraseMode::Phrased;
        opts.rhythm = RhythmMode::Flowing;
        opts.energy = 0.5;         // energy scale == 1.0 -> the cap is exact
        opts.arpeggioAmount = 0.0; // isolate the contour from ornaments
        std::mt19937 rng(seed);
        const Melody m = generateMelody(grid, scale, opts, rng);

        const std::vector<std::size_t>& starts = m.phraseStarts;
        CHECK(!starts.empty());
        for (std::size_t p = 0; p < starts.size(); ++p) {
            const std::size_t begin = starts[p];
            const std::size_t end =
                (p + 1 < starts.size()) ? starts[p + 1] : m.notes.size();
            for (std::size_t i = begin + 1; i < end; ++i) {
                const int d =
                    std::abs(m.notes[i].velocity - m.notes[i - 1].velocity);
                CHECK(d <= kMaxDynStep);  // no per-beat stab within a phrase
                if (d > maxWithinPhraseJump) maxWithinPhraseJump = d;
            }
        }
        for (const Note& n : m.notes) {
            lo = std::min(lo, n.velocity);
            hi = std::max(hi, n.velocity);
        }
    }
    // Expression preserved: the fix removes whiplash, not the dynamic range.
    CHECK(hi - lo >= 30);
    // And the cap is genuinely exercised (guards against a vacuously flat pass).
    CHECK(maxWithinPhraseJump >= 8);
}

// ---- phrased mode: the final tonic is approached by step, not a leap --------

void test_phrased_ending_is_stepwise() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    for (unsigned seed = 1; seed <= 40; ++seed) {
        MelodyOptions opts;
        opts.length = 24;
        opts.phraseMode = PhraseMode::Phrased;

        std::mt19937 rng(seed);
        const Melody melody = generateMelody(grid, scale, opts, rng);
        CHECK(melody.degrees.size() >= 2);

        const std::size_t n = melody.degrees.size();
        const int delta = melody.degrees[n - 1] - melody.degrees[n - 2];
        // A single scale step into the tonic (a real step, never a leap or a
        // repeated note): the two-octave range always leaves room.
        CHECK(std::abs(delta) >= 1);
        CHECK(std::abs(delta) <= 2);
    }
}

// ---- source cells: parallel track, in range, and tracking the walk ----------

// True when cells `a` and `b` are 8-connected neighbours on a cols x rows grid
// whose edges wrap — i.e. the per-axis distance is 0, 1, or a full wrap
// (cols-1 / rows-1), and they are not the same cell.
bool wrapAdjacent(const lumena::melody::GridCell& a,
                  const lumena::melody::GridCell& b, int cols, int rows) {
    auto axisOk = [](int p, int q, int span) {
        const int d = std::abs(p - q);
        return d == 0 || d == 1 || d == span - 1;
    };
    if (a.col == b.col && a.row == b.row) return false;  // the walk always moves
    return axisOk(a.col, b.col, cols) && axisOk(a.row, b.row, rows);
}

void test_cells_track_walk_freeform() {
    const Image image = makeDiagonalGradient(160, 120);
    const int cols = 16, rows = 12;
    const BrightnessGrid grid(image, cols, rows);
    const Scale scale = minorPentatonic();

    MelodyOptions opts;
    opts.length = 40;
    opts.phraseMode = PhraseMode::Freeform;
    opts.cellPath = CellPath::RandomWalk;

    std::mt19937 rng(12345u);
    const Melody melody = generateMelody(grid, scale, opts, rng);

    // One source cell per note.
    CHECK(melody.cells.size() == melody.notes.size());

    bool inRange = true;
    for (const auto& c : melody.cells) {
        if (c.col < 0 || c.col >= cols || c.row < 0 || c.row >= rows)
            inRange = false;
    }
    CHECK(inRange);

    // The first note sits on the centre cell; every step after is an 8-connected
    // (wrapping) neighbour of the previous cell — the cells trace the walk.
    CHECK(melody.cells[0].col == cols / 2);
    CHECK(melody.cells[0].row == rows / 2);
    bool adjacent = true;
    for (std::size_t i = 1; i < melody.cells.size(); ++i) {
        if (!wrapAdjacent(melody.cells[i - 1], melody.cells[i], cols, rows))
            adjacent = false;
    }
    CHECK(adjacent);
}

void test_cells_in_range_pure_random() {
    const Image image = makeDiagonalGradient(160, 120);
    const int cols = 16, rows = 12;
    const BrightnessGrid grid(image, cols, rows);
    const Scale scale = minorPentatonic();

    MelodyOptions opts;
    opts.length = 30;
    opts.phraseMode = PhraseMode::Freeform;
    opts.cellPath = CellPath::PureRandom;

    std::mt19937 rng(2u);
    const Melody melody = generateMelody(grid, scale, opts, rng);

    CHECK(melody.cells.size() == melody.notes.size());
    bool inRange = true;
    std::set<int> distinctCols;
    for (const auto& c : melody.cells) {
        if (c.col < 0 || c.col >= cols || c.row < 0 || c.row >= rows)
            inRange = false;
        distinctCols.insert(c.col);
    }
    CHECK(inRange);
    CHECK(distinctCols.size() > 1);  // pure-random cells jump around
}

void test_cells_reproducible() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions opts;
    opts.length = 40;
    opts.phraseMode = PhraseMode::Phrased;  // exercise the phrased cell path too

    std::mt19937 r1(99u);
    std::mt19937 r2(99u);
    const Melody a = generateMelody(grid, scale, opts, r1);
    const Melody b = generateMelody(grid, scale, opts, r2);

    bool same = a.cells.size() == b.cells.size();
    for (std::size_t i = 0; i < a.cells.size() && same; ++i) {
        if (a.cells[i].col != b.cells[i].col ||
            a.cells[i].row != b.cells[i].row)
            same = false;
    }
    CHECK(same);
}

void test_cells_phrased_in_range_and_share_motif() {
    const Image image = makeDiagonalGradient(160, 120);
    const int cols = 16, rows = 12;
    const BrightnessGrid grid(image, cols, rows);
    const Scale scale = minorPentatonic();

    MelodyOptions opts;
    opts.length = 32;
    opts.phraseMode = PhraseMode::Phrased;
    opts.arpeggioAmount = 0.0;  // keep phrase note counts exact for comparison

    std::mt19937 rng(2024u);
    const Melody melody = generateMelody(grid, scale, opts, rng);

    CHECK(melody.cells.size() == melody.notes.size());
    bool inRange = true;
    for (const auto& c : melody.cells) {
        if (c.col < 0 || c.col >= cols || c.row < 0 || c.row >= rows)
            inRange = false;
    }
    CHECK(inRange);

    // A' (a phrase whose degree deltas match the motif) is a transposition of
    // the motif and re-traces the motif's cells — so a matching-delta phrase
    // must carry cell-for-cell identical source cells.
    const std::vector<std::size_t>& starts = melody.phraseStarts;
    CHECK(starts.size() >= 4);
    const std::vector<int> motif =
        phraseDeltas(melody.degrees, starts[0], starts[1]);
    const std::size_t motifLen = starts[1] - starts[0];

    bool checkedRepeat = false;
    for (std::size_t p = 1; p + 1 < starts.size(); ++p) {
        if (starts[p + 1] - starts[p] != motifLen) continue;
        if (phraseDeltas(melody.degrees, starts[p], starts[p + 1]) != motif)
            continue;
        bool sameCells = true;
        for (std::size_t k = 0; k < motifLen; ++k) {
            if (melody.cells[starts[0] + k].col !=
                    melody.cells[starts[p] + k].col ||
                melody.cells[starts[0] + k].row !=
                    melody.cells[starts[p] + k].row)
                sameCells = false;
        }
        CHECK(sameCells);
        checkedRepeat = true;
        break;
    }
    CHECK(checkedRepeat);
}

// ---- arpeggiator ------------------------------------------------------------

// The set of all in-scale MIDI notes across `span` octaves.
// The pitch-class set of the key's diatonic (major/natural-minor) scale — the
// harmony the chords/arp draw from, whatever the melodic scale.
std::set<int> keyScalePcs(const Scale& scale) {
    const int major[7] = {0, 2, 4, 5, 7, 9, 11};
    const int minor[7] = {0, 2, 3, 5, 7, 8, 10};
    bool hasM3 = false, hasm3 = false;
    for (int iv : scale.intervals) {
        const int pc = ((iv % 12) + 12) % 12;
        if (pc == 4) hasM3 = true;
        if (pc == 3) hasm3 = true;
    }
    const int* st = (hasM3 && ! hasm3) ? major : minor;
    const int tonicPc = ((scale.rootNote % 12) + 12) % 12;
    std::set<int> pcs;
    for (int k = 0; k < 7; ++k) pcs.insert((tonicPc + st[k]) % 12);
    return pcs;
}

// True if three MIDI notes form a tertian triad by pitch class (some root with a
// third and a fifth above), regardless of octave/inversion.
bool isTriadByPitchClass(int a, int b, int c) {
    std::set<int> pcs = {a % 12, b % 12, c % 12};
    if (pcs.size() != 3) return false;
    for (int root : pcs) {
        const bool third = pcs.count((root + 3) % 12) || pcs.count((root + 4) % 12);
        const bool fifth = pcs.count((root + 6) % 12) || pcs.count((root + 7) % 12);
        if (third && fifth) return true;
    }
    return false;
}

// The diatonic triad tones the arp/chords build: root/third/fifth of the key's
// tonic chord (major or minor per the scale) across `octaves`, voiced from MIDI
// 48 — matching the engine's diatonicChord().
std::set<int> diatonicTriadTones(const Scale& scale, int octaves) {
    const int major[7] = {0, 2, 4, 5, 7, 9, 11};
    const int minor[7] = {0, 2, 3, 5, 7, 8, 10};
    bool hasM3 = false, hasm3 = false;
    for (int iv : scale.intervals) {
        const int pc = ((iv % 12) + 12) % 12;
        if (pc == 4) hasM3 = true;
        if (pc == 3) hasm3 = true;
    }
    const int* st = (hasM3 && ! hasm3) ? major : minor;
    const int tonicPc = ((scale.rootNote % 12) + 12) % 12;
    std::set<int> notes;
    for (int o = 0; o < octaves; ++o)
        for (int k = 0; k < 3; ++k)  // root, third, fifth
            notes.insert(55 + 12 * o + tonicPc + st[(2 * k) % 7]);
    return notes;
}

// Arp notes are chord tones drawn from the key, with velocity accents (not flat)
// and a steady eighth-note count.
void test_arpeggiator_chord_tones_and_rate() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions o;
    o.mode = lumena::melody::GenerationMode::Arpeggio;
    o.arpPattern = ArpPattern::Up;
    o.arpOctaves = 2;
    o.arpRate = 0.5;
    o.loopBars = 2;  // 2 bars of eighths = 16 notes
    std::mt19937 rng(11u);
    const Melody m = generateMelody(grid, scale, o, rng);

    CHECK(m.notes.size() == 16);
    const std::set<int> keyPcs = keyScalePcs(scale);
    std::set<int> vels;
    for (const Note& n : m.notes) {
        CHECK(keyPcs.count(n.noteNumber % 12) == 1);  // in the key (any octave)
        vels.insert(n.velocity);
    }
    CHECK(vels.size() > 1);  // accents present, not flat velocity
    CHECK(m.cells.size() == m.notes.size());
}

// The arp follows a moving progression: the chord changes across bars.
void test_arpeggiator_progression_moves() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions o;
    o.mode = lumena::melody::GenerationMode::Arpeggio;
    o.arpPattern = ArpPattern::Up;
    o.arpRate = 0.5;
    o.loopBars = 4;
    std::mt19937 rng(5u);
    const Melody m = generateMelody(grid, scale, o, rng);

    // Lowest note per bar (8 notes/bar); at least two distinct bass pitch classes
    // means the harmony actually moves rather than a static chord.
    std::set<int> bassPcs;
    const int perBar = 8;
    for (int bar = 0; bar * perBar < static_cast<int>(m.notes.size()); ++bar) {
        int lo = 128;
        for (int j = 0; j < perBar; ++j) {
            const std::size_t idx = static_cast<std::size_t>(bar * perBar + j);
            if (idx < m.notes.size()) lo = std::min(lo, m.notes[idx].noteNumber);
        }
        if (lo < 128) bassPcs.insert(lo % 12);
    }
    CHECK(bassPcs.size() >= 2);
}

// ---- loop -------------------------------------------------------------------

double totalBeats(const Melody& m) {
    double t = 0.0;
    for (const Note& n : m.notes) t = std::max(t, n.startBeats + n.lengthBeats);
    return t;
}

// Loop mode fills exactly loopBars bars, for the arpeggiator, chords, and the
// phrased walk.
void test_loop_fills_whole_bars() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    for (int bars : {1, 2, 4, 8}) {
        MelodyOptions arp;
        arp.mode = lumena::melody::GenerationMode::Arpeggio;
        arp.arpRate = 0.5;
        arp.length = 7;  // ignored: loopBars drives the count
        arp.loopBars = bars;
        arp.beatsPerBar = 4.0;
        std::mt19937 rng(2u);
        const Melody m = generateMelody(grid, scale, arp, rng);
        CHECK(std::abs(totalBeats(m) - bars * 4.0) < 1e-6);
    }

    // Chords: whole bars too, with every chord a stack of notes at one start.
    {
        MelodyOptions ch;
        ch.mode = lumena::melody::GenerationMode::Chords;
        ch.chordRate = 2.0;
        ch.chordSize = 3;
        ch.loopBars = 4;
        ch.beatsPerBar = 4.0;
        std::mt19937 rng(9u);
        const Melody m = generateMelody(grid, scale, ch, rng);
        CHECK(std::abs(totalBeats(m) - 4 * 4.0) < 1e-6);
        CHECK(m.notes.size() % 3 == 0);  // triads
    }

    MelodyOptions phrased;
    phrased.length = 16;
    phrased.loopBars = 4;
    phrased.beatsPerBar = 4.0;
    std::mt19937 rng(2u);
    const Melody m = generateMelody(grid, scale, phrased, rng);
    const double bars = totalBeats(m) / phrased.beatsPerBar;
    CHECK(std::abs(bars - std::round(bars)) < 1e-6);
    CHECK(bars >= 4.0);  // at least the requested loop length
}

// Arpeggiator output is reproducible under a fixed seed.
void test_arpeggiator_reproducible() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions o;
    o.mode = lumena::melody::GenerationMode::Arpeggio;
    o.arpPattern = ArpPattern::Random;
    o.length = 16;
    std::mt19937 a(42u), b(42u);
    const Melody ma = generateMelody(grid, scale, o, a);
    const Melody mb = generateMelody(grid, scale, o, b);
    CHECK(ma.notes.size() == mb.notes.size());
    bool identical = ma.notes.size() == mb.notes.size();
    for (std::size_t i = 0; i < ma.notes.size() && identical; ++i) {
        if (ma.notes[i].noteNumber != mb.notes[i].noteNumber ||
            ma.notes[i].velocity != mb.notes[i].velocity) {
            identical = false;
        }
    }
    CHECK(identical);
}

// ---- chords -----------------------------------------------------------------

// Chords mode emits groups of `chordSize` in-scale notes that share a start
// Chords mode emits real triads in the key: each is `chordSize` notes sharing a
// start beat, forming a tertian triad by pitch class (any inversion), all tones
// diatonic to the key, with audible velocity.
void test_chords_are_diatonic_stacks() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions o;
    o.mode = lumena::melody::GenerationMode::Chords;
    o.chordSize = 3;
    o.energy = 0.5;      // one chord per bar
    o.loopBars = 4;
    o.beatsPerBar = 4.0;
    std::mt19937 rng(4u);
    const Melody m = generateMelody(grid, scale, o, rng);

    CHECK(m.notes.size() % 3 == 0);
    CHECK(m.notes.size() >= 12);
    const std::set<int> keyPcs = keyScalePcs(scale);
    for (std::size_t c = 0; c < m.notes.size(); c += 3) {
        const double start = m.notes[c].startBeats;
        for (int v = 0; v < 3; ++v) {
            CHECK(m.notes[c + v].startBeats == start);           // sound together
            CHECK(m.notes[c + v].velocity >= 60);                // present
            CHECK(keyPcs.count(m.notes[c + v].noteNumber % 12) == 1);  // in key
        }
        CHECK(isTriadByPitchClass(m.notes[c].noteNumber,
                                  m.notes[c + 1].noteNumber,
                                  m.notes[c + 2].noteNumber));   // real triad
    }
}

// ---- bug 5: harmonic-minor leading tone (FAILING until the fix) --------------
// Chords/Arp spell diatonic triads from kMajorSteps/kMinorSteps chosen by
// scaleIsMajor(), never consulting the detected scale's own intervals. So a
// harmonic-minor scale is spelled as natural minor, losing its raised 7th: the V
// chord comes out minor (A-C-E) instead of major (A-C#-E) and the leading tone
// (pitch class 1 for D harmonic minor) is never emitted. The V degree is in
// every progression template, so ~4 bars always renders it — a correct spelling
// WOULD emit pitch class 1. These two tests assert it does; they FAIL today.
void test_chords_spell_harmonic_minor_leading_tone() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = dHarmonicMinor();
    const int leadingTonePc = ((scale.rootNote % 12) + 11) % 12;  // C# = 1

    MelodyOptions o;
    o.mode = lumena::melody::GenerationMode::Chords;
    o.chordSize = 3;
    o.energy = 0.5;      // one chord per bar
    o.loopBars = 4;      // one full I/IV/V/vi progression cycle
    o.beatsPerBar = 4.0;
    std::mt19937 rng(4u);
    const Melody m = generateMelody(grid, scale, o, rng);

    std::set<int> pcs;
    for (const Note& n : m.notes) pcs.insert(((n.noteNumber % 12) + 12) % 12);
    // The raised 7th (leading tone) must appear — it is the third of the V chord.
    CHECK(pcs.count(leadingTonePc) == 1);
}

void test_arpeggio_spells_harmonic_minor_leading_tone() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = dHarmonicMinor();
    const int leadingTonePc = ((scale.rootNote % 12) + 11) % 12;  // C# = 1

    MelodyOptions o;
    o.mode = lumena::melody::GenerationMode::Arpeggio;
    o.arpPattern = ArpPattern::Up;
    o.arpOctaves = 2;
    o.arpRate = 0.5;
    o.loopBars = 4;      // 4 bars of eighths = 32 notes, one full progression cycle
    std::mt19937 rng(11u);
    const Melody m = generateMelody(grid, scale, o, rng);

    std::set<int> pcs;
    for (const Note& n : m.notes) pcs.insert(((n.noteNumber % 12) + 12) % 12);
    CHECK(pcs.count(leadingTonePc) == 1);
}

// ---- Phase 4a: scale-aware arps --------------------------------------------
// Blues is a 6-note scale, so it falls through diatonicChord's non-7-degree
// fallback which spells a plain minor triad and drops the ♭7 blue note. The arp
// must instead voice a real minor-7th (root-♭3-5-♭7) so the ♭7 sounds. The
// seventh is recorded as chord-tone role 3 (0=root,1=third,2=fifth,3=seventh),
// which a triad-only arp never produces; and every emitted tone must stay inside
// the parent minor key (no chromatic tone introduced to "complete" the chord).
void test_arpeggio_blues_spells_in_scale_seventh() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = aBlues();

    MelodyOptions o;
    o.mode = lumena::melody::GenerationMode::Arpeggio;
    o.arpPattern = ArpPattern::Up;
    o.arpOctaves = 2;
    o.arpRate = 0.5;
    o.loopBars = 4;      // one full progression cycle: every chord degree renders
    std::mt19937 rng(11u);
    const Melody m = generateMelody(grid, scale, o, rng);

    // A seventh (role 3) is spelled somewhere — the blue ♭7 sounds.
    bool sawSeventh = false;
    for (int role : m.chordTones)
        if (role == 3) sawSeventh = true;
    CHECK(sawSeventh);

    // Nothing chromatic: every tone is diatonic to the parent minor key.
    const std::set<int> keyPcs = keyScalePcs(scale);
    for (const Note& n : m.notes)
        CHECK(keyPcs.count(((n.noteNumber % 12) + 12) % 12) == 1);
}

// A triad-flavoured scale (minor pentatonic) arp resolves up to the octave: the
// ascent spells 1-3-5-8, so the root pitch class appears at two octaves a 12
// apart within a single-octave figure. On the pre-4a arp (1-3-5, no cap) the
// one-octave figure has a single root, so this fails; the octave cap makes it
// pass. A flat grey image keeps the arp off the brightness octave-jump path so
// the pitches are exactly the cycle. Triad scales stay triads: no seventh.
void test_arpeggio_resolves_to_octave() {
    const Image image = makeGrey(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions o;
    o.mode = lumena::melody::GenerationMode::Arpeggio;
    o.arpPattern = ArpPattern::Up;
    o.arpOctaves = 1;    // one octave, so the "8" can only come from the cap
    o.arpRate = 0.5;
    o.loopBars = 2;
    std::mt19937 rng(7u);
    const Melody m = generateMelody(grid, scale, o, rng);

    // The lowest note is a chord root (role 0); the same pitch class recurs an
    // octave above it — the 8 of 1-3-5-8.
    int lowRoot = 128;
    for (std::size_t i = 0; i < m.notes.size(); ++i)
        if (m.chordTones[i] == 0) lowRoot = std::min(lowRoot, m.notes[i].noteNumber);
    CHECK(lowRoot < 128);
    bool sawOctave = false;
    for (const Note& n : m.notes)
        if (n.noteNumber == lowRoot + 12) sawOctave = true;
    CHECK(sawOctave);

    // A pure triad scale never spells a seventh.
    for (int role : m.chordTones) CHECK(role != 3);
}

// ---- Phase 2: brightness -> pitch blend (FAILING until the blend lands) ------
// The product promise: at Image Influence = 1.0 the melody should track the
// image. Concretely, in PHRASED mode a walked note's scale degree should equal
// the degree its source cell's brightness maps to (scales::mapBrightnessToDegree,
// exactly as Freeform already does at MelodyGenerator.cpp:182-183). Today Phrased
// mode has NO brightness->degree blend: its only image coupling is a weak,
// direction-only +-1 gradient nudge (MelodyGenerator.cpp:632), which never sets
// the degree to the brightness target. So raising Image Influence from 0.0 to
// 1.0 barely changes how many notes land on their cell's brightness degree.
//
// This test counts, over many seeds, how many emitted notes sit exactly on
// mapBrightnessToDegree(source_brightness) at Influence 0.0 vs 1.0, and asserts
// the 1.0 count is CLEARLY greater (at least double). On 9242e9c the two counts
// are ~equal, so it FAILS; once the blend
//   finalDegree = snapToScale(blend(markovDegree, imageTargetDegree, influence))
// is added (Influence 1.0 => degree == imageTarget on walked notes), it passes.
void test_phrased_tracks_brightness_at_high_influence() {
    const Image image = makeDiagonalGradient(160, 120);
    const int cols = 16, rows = 12;
    const BrightnessGrid grid(image, cols, rows);
    const Scale scale = minorPentatonic();
    const int octaveSpan = 2;                              // MelodyOptions default
    const int totalDegrees = scale.usableDegrees(octaveSpan);

    // Count notes whose degree == the degree their source cell's brightness maps
    // to, summed across a band of seeds so the rate is stable, not seed-luck.
    auto brightnessMatches = [&](double influence) {
        long matches = 0;
        long notes = 0;
        for (unsigned seed = 1; seed <= 40; ++seed) {
            MelodyOptions opts;
            opts.length = 40;
            opts.octaveSpan = octaveSpan;
            opts.phraseMode = PhraseMode::Phrased;
            opts.rhythm = RhythmMode::Flowing;
            opts.cellPath = CellPath::RandomWalk;
            opts.arpeggioAmount = 0.0;        // no ornaments/leaps: isolate the blend
            opts.brightnessBias = influence;  // "Image Influence"

            std::mt19937 rng(seed);
            const Melody m = generateMelody(grid, scale, opts, rng);

            for (std::size_t i = 0; i < m.degrees.size(); ++i) {
                // Reconstruct the note's source brightness exactly as the engine
                // sampled it: from the grid cell recorded on the note.
                const float b = grid.valueAt(m.cells[i].col, m.cells[i].row);
                if (m.degrees[i] == mapBrightnessToDegree(b, totalDegrees))
                    ++matches;
                ++notes;
            }
        }
        std::printf("    [influence=%.1f] %ld/%ld notes on their brightness degree\n",
                    influence, matches, notes);
        return matches;
    };

    const long matchesLow = brightnessMatches(0.0);
    const long matchesHigh = brightnessMatches(1.0);

    // Sanity: the 0.0 baseline has some incidental matches to divide against
    // (guards the "at least double" ratio below from a trivial 0-vs-0 pass).
    CHECK(matchesLow > 0);
    // The core assertion: Image Influence = 1.0 tracks brightness CLEARLY more
    // than Influence = 0.0. "Clearly" = at least double the matching notes.
    CHECK(matchesHigh > matchesLow);
    CHECK(matchesHigh >= 2 * matchesLow);
}

// ---- Phase 3: image-driven rhythmic density ---------------------------------

// Every note (start and length) lands on an integer tick of the 960 grid.
bool allOnTickGrid(const Melody& m) {
    constexpr double kTicks = 960.0;
    for (const Note& n : m.notes) {
        const double s = n.startBeats * kTicks;
        const double l = n.lengthBeats * kTicks;
        if (std::fabs(s - std::round(s)) > 1e-6) return false;
        if (std::fabs(l - std::round(l)) > 1e-6) return false;
    }
    return true;
}

// imageRhythmAmount subdivides notes in high-contrast regions and does nothing
// in flat ones, always on the grid, always in scale, and is a byte-exact no-op
// at amount 0 (so the RNG stream and groove-only output are untouched).
void test_phrased_image_density() {
    const int cols = 16, rows = 12;
    const Scale scale = minorPentatonic();
    std::set<int> scalePcs;
    for (int iv : scale.intervals) scalePcs.insert(((iv % 12) + 12) % 12);

    const BrightnessGrid busy(makeCheckerboard(160, 120, 10), cols, rows);
    const BrightnessGrid flat(makeBright(160, 120), cols, rows);

    auto gen = [&](const BrightnessGrid& g, double amount, unsigned seed) {
        MelodyOptions o;
        o.length = 40;
        o.phraseMode = PhraseMode::Phrased;
        o.rhythm = RhythmMode::Flowing;
        o.arpeggioAmount = 0.0;  // isolate density from ornaments
        o.imageRhythmAmount = amount;
        std::mt19937 rng(seed);
        return generateMelody(g, scale, o, rng);
    };

    long denserSeeds = 0;
    for (unsigned seed = 1; seed <= 40; ++seed) {
        const Melody off = gen(busy, 0.0, seed);
        const Melody on = gen(busy, 1.0, seed);

        // Density only adds notes (post-walk, no RNG): same seed => the walk and
        // its note count are identical, subdivisions only split existing notes.
        CHECK(on.notes.size() >= off.notes.size());
        if (on.notes.size() > off.notes.size()) ++denserSeeds;

        // On-grid and in-scale even with maximal subdivision.
        CHECK(allOnTickGrid(on));
        for (const Note& n : on.notes) {
            const int pc = ((n.noteNumber - scale.rootNote) % 12 + 12) % 12;
            CHECK(scalePcs.count(pc) == 1);
        }

        // The split conserves total time: no beats invented or lost.
        CHECK(std::abs(totalBeats(on) - totalBeats(off)) < 1e-6);

        // A flat image has no local contrast, so density can't fire: the note
        // count matches the amount-0 run exactly.
        const Melody flatOff = gen(flat, 0.0, seed);
        const Melody flatOn = gen(flat, 1.0, seed);
        CHECK(flatOn.notes.size() == flatOff.notes.size());
    }
    // The checkerboard is high-contrast everywhere, so density fires broadly.
    CHECK(denserSeeds >= 35);
}

// Regression guard: image density is a pure function of the image and draws no
// RNG, so raising imageRhythmAmount never perturbs the walk's seed stream. Same
// seed + image => the mt19937 is left in an identical state whatever the amount,
// so the non-density path (pitches, rests, ornaments) reproduces bit-for-bit.
void test_image_density_draws_no_rng() {
    const BrightnessGrid busy(makeCheckerboard(160, 120, 10), 16, 12);
    const Scale scale = minorPentatonic();
    for (unsigned seed = 1; seed <= 40; ++seed) {
        auto endState = [&](double amount) {
            MelodyOptions o;
            o.length = 48;
            o.phraseMode = PhraseMode::Phrased;
            o.rhythm = RhythmMode::Flowing;
            o.imageRhythmAmount = amount;
            std::mt19937 rng(seed);
            generateMelody(busy, scale, o, rng);
            return rng();  // one more draw reveals the post-generation state
        };
        const std::mt19937::result_type off = endState(0.0);
        CHECK(endState(0.5) == off);
        CHECK(endState(1.0) == off);
    }
}

// Phase 3.5: image density composes MELODIC CONTENT (passing/neighbour tones),
// not unison chops. A subdivided note's extra pieces share its source cell but
// must move in pitch (a passing run toward the next note, or a neighbour turn),
// while every piece stays in scale. Regression guard for the "density chops
// instead of composing" fix.
void test_density_composes_melodic_content() {
    const int cols = 16, rows = 12;
    const Scale scale = minorPentatonic();
    std::set<int> scalePcs;
    for (int iv : scale.intervals) scalePcs.insert(((iv % 12) + 12) % 12);
    const BrightnessGrid busy(makeCheckerboard(160, 120, 10), cols, rows);

    long splitGroups = 0;    // adjacent notes sharing a source cell (a split)
    long movedGroups = 0;    // ...of which at least one piece moved in pitch
    for (unsigned seed = 1; seed <= 40; ++seed) {
        MelodyOptions o;
        o.length = 40;
        o.phraseMode = PhraseMode::Phrased;
        o.rhythm = RhythmMode::Flowing;
        o.arpeggioAmount = 0.0;
        o.imageRhythmAmount = 1.0;
        std::mt19937 rng(seed);
        const Melody m = generateMelody(busy, scale, o, rng);

        // Every emitted note is in scale, split or not.
        for (const Note& n : m.notes) {
            const int pc = ((n.noteNumber - scale.rootNote) % 12 + 12) % 12;
            CHECK(scalePcs.count(pc) == 1);
        }

        // Walk runs of consecutive notes that share a source cell (one split
        // group) and check the group is not a single held pitch.
        for (std::size_t i = 1; i < m.notes.size(); ++i) {
            const bool sameCell = m.cells[i].col == m.cells[i - 1].col &&
                                  m.cells[i].row == m.cells[i - 1].row;
            if (!sameCell) continue;
            ++splitGroups;
            if (m.degrees[i] != m.degrees[i - 1]) ++movedGroups;
        }
    }
    // The checkerboard subdivides broadly, so there are plenty of split groups,
    // and the fill moves in pitch rather than repeating one note (old chop
    // behaviour would leave movedGroups == 0).
    CHECK(splitGroups > 50);
    CHECK(movedGroups > 0);
    // At least half of all split-group steps are melodic motion, not unison.
    CHECK(movedGroups * 2 >= splitGroups);
}

// Phase 3.5: the two-bar syncopated rhythm templates keep every note on the
// 960-tick grid across the whole Energy range (the dotted-eighth 3-3-2 pushes
// and long-short-short answers are all exact tick fractions). Guards against a
// future template whose slot lengths would drift off-grid, and confirms the
// off-beat (syncopated) onsets the phase adds still land on integer ticks so
// the pass-2 reconciliation sees clean beats.
void test_phrased_syncopation_on_grid() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();
    for (double energy : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        for (unsigned seed = 1; seed <= 30; ++seed) {
            MelodyOptions o;
            o.length = 48;
            o.phraseMode = PhraseMode::Phrased;
            o.rhythm = RhythmMode::Flowing;
            o.energy = energy;
            std::mt19937 rng(seed);
            const Melody m = generateMelody(grid, scale, o, rng);
            CHECK(allOnTickGrid(m));
        }
    }
}

// ---- semantic axes ----------------------------------------------------------

// Higher Energy raises overall velocity.
void test_energy_raises_velocity() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    auto meanVel = [&] (double energy) {
        MelodyOptions o;
        o.phraseMode = PhraseMode::Freeform;
        o.length = 24;
        o.energy = energy;
        std::mt19937 rng(3u);
        const Melody m = generateMelody(grid, scale, o, rng);
        long sum = 0;
        for (const Note& n : m.notes) sum += n.velocity;
        return static_cast<double>(sum) / static_cast<double>(m.notes.size());
    };

    CHECK(meanVel(0.9) > meanVel(0.1));
}

// Repetition = 1 makes at least one body phrase repeat the motif verbatim.
void test_repetition_repeats_motif() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions o;
    o.phraseMode = PhraseMode::Phrased;
    o.length = 32;
    o.repetition = 1.0;
    o.arpeggioAmount = 0.0;  // no ornaments, so a repeat stays verbatim
    std::mt19937 rng(1u);
    const Melody m = generateMelody(grid, scale, o, rng);

    CHECK(m.phraseStarts.size() >= 3);
    // Motif = the first phrase's degree sequence.
    const std::size_t m0 = m.phraseStarts[0];
    const std::size_t m1 = m.phraseStarts[1];
    std::vector<int> motif (m.degrees.begin() + static_cast<long>(m0),
                            m.degrees.begin() + static_cast<long>(m1));
    int verbatim = 0;
    for (std::size_t p = 1; p + 1 < m.phraseStarts.size(); ++p) {
        const std::size_t s = m.phraseStarts[p];
        const std::size_t e = m.phraseStarts[p + 1];
        std::vector<int> seg (m.degrees.begin() + static_cast<long>(s),
                              m.degrees.begin() + static_cast<long>(e));
        if (seg == motif) ++verbatim;
    }
    CHECK(verbatim >= 1);
}

// ---- lock-aware recombination + mutation ------------------------------------

void test_recombine_locks_dimensions() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions o;
    o.phraseMode = PhraseMode::Freeform;  // fixed length: base and cand align
    o.length = 16;
    std::mt19937 a(7u), b(99u);
    const Melody base = generateMelody(grid, scale, o, a);
    const Melody cand = generateMelody(grid, scale, o, b);

    // Lock rhythm: timings from base, pitches from candidate.
    const Melody lr = recombineLocked(base, cand, scale, { true, false }, o);
    bool timingFromBase = true, pitchFromCand = true;
    for (std::size_t i = 0; i < lr.notes.size(); ++i) {
        if (lr.notes[i].startBeats != base.notes[i].startBeats ||
            lr.notes[i].lengthBeats != base.notes[i].lengthBeats)
            timingFromBase = false;
        if (lr.notes[i].noteNumber != cand.notes[i].noteNumber)
            pitchFromCand = false;
    }
    CHECK(timingFromBase);
    CHECK(pitchFromCand);

    // Lock pitch: pitches from base, timings from candidate.
    const Melody lp = recombineLocked(base, cand, scale, { false, true }, o);
    bool pitchFromBase = true, timingFromCand = true;
    for (std::size_t i = 0; i < lp.notes.size(); ++i) {
        if (lp.notes[i].noteNumber != base.notes[i].noteNumber)
            pitchFromBase = false;
        if (lp.notes[i].startBeats != cand.notes[i].startBeats ||
            lp.notes[i].lengthBeats != cand.notes[i].lengthBeats)
            timingFromCand = false;
    }
    CHECK(pitchFromBase);
    CHECK(timingFromCand);

    // Both locked -> unchanged base; neither -> the candidate.
    const Melody both = recombineLocked(base, cand, scale, { true, true }, o);
    CHECK(both.notes.size() == base.notes.size());
    const Melody none = recombineLocked(base, cand, scale, { false, false }, o);
    CHECK(none.notes.size() == cand.notes.size());
}

void test_mutate_respects_locks() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions o;
    o.phraseMode = PhraseMode::Freeform;
    o.length = 24;
    std::mt19937 g(5u);
    const Melody base = generateMelody(grid, scale, o, g);

    // Lock pitch: mutate may retime but never repitches.
    std::mt19937 r1(11u);
    const Melody mp = mutate(base, scale, { false, true }, 0.6, o, r1);
    bool pitchesHeld = mp.notes.size() == base.notes.size();
    for (std::size_t i = 0; i < mp.notes.size() && pitchesHeld; ++i)
        if (mp.notes[i].noteNumber != base.notes[i].noteNumber)
            pitchesHeld = false;
    CHECK(pitchesHeld);

    // Lock rhythm: mutate may repitch but never retimes.
    std::mt19937 r2(11u);
    const Melody mr = mutate(base, scale, { true, false }, 0.6, o, r2);
    bool timingHeld = mr.notes.size() == base.notes.size();
    for (std::size_t i = 0; i < mr.notes.size() && timingHeld; ++i)
        if (mr.notes[i].startBeats != base.notes[i].startBeats ||
            mr.notes[i].lengthBeats != base.notes[i].lengthBeats)
            timingHeld = false;
    CHECK(timingHeld);

    // Mutating some notes actually changed at least one pitch here.
    bool anyPitchChange = false;
    for (std::size_t i = 0; i < mr.notes.size(); ++i)
        if (mr.notes[i].noteNumber != base.notes[i].noteNumber)
            anyPitchChange = true;
    CHECK(anyPitchChange);
}

// ---- Phase 4b: splice-lock determinism + count matching --------------------

// True if two melodies are byte-identical in the emitted note track.
bool melodyNotesIdentical(const Melody& a, const Melody& b) {
    if (a.notes.size() != b.notes.size()) return false;
    for (std::size_t i = 0; i < a.notes.size(); ++i) {
        if (a.notes[i].noteNumber != b.notes[i].noteNumber) return false;
        if (a.notes[i].velocity != b.notes[i].velocity) return false;
        if (a.notes[i].startBeats != b.notes[i].startBeats) return false;
        if (a.notes[i].lengthBeats != b.notes[i].lengthBeats) return false;
    }
    return true;
}

// A melody with the given pitches, one quarter note each (so the timing track is
// well-defined and distinct per index — lets a splice test read both tracks).
Melody melodyOfPitches(const std::vector<int>& pitches) {
    Melody m;
    double beat = 0.0;
    for (int p : pitches) {
        Note n;
        n.noteNumber = p;
        n.velocity = 80;
        n.startBeats = beat;
        n.lengthBeats = 1.0;
        m.notes.push_back(n);
        m.degrees.push_back(0);
        m.cells.push_back(lumena::melody::GridCell{});
        beat += 1.0;
    }
    return m;
}

// recombineLocked draws no RNG, so identical (previous, candidate, locks) inputs
// splice to a byte-identical melody; mutate is deterministic given its seed and
// varies with it. This is the "Regenerate is reproducible" guarantee at the
// splice layer (generation reproducibility is covered by test_reproducible).
void test_splice_locks_deterministic() {
    const Image image = makeDiagonalGradient(160, 120);
    const BrightnessGrid grid(image, 16, 12);
    const Scale scale = minorPentatonic();

    MelodyOptions o;
    o.phraseMode = PhraseMode::Freeform;
    o.length = 20;
    std::mt19937 a(3u), b(4u);
    const Melody prev = generateMelody(grid, scale, o, a);
    const Melody cand = generateMelody(grid, scale, o, b);

    const RegenLocks lockRhythm{ true, false };
    const Melody s1 = recombineLocked(prev, cand, scale, lockRhythm, o);
    const Melody s2 = recombineLocked(prev, cand, scale, lockRhythm, o);
    CHECK(melodyNotesIdentical(s1, s2));  // pure: same in -> same out

    std::mt19937 m1(55u), m2(55u);
    const Melody u1 = mutate(prev, scale, { false, false }, 0.4, o, m1);
    const Melody u2 = mutate(prev, scale, { false, false }, 0.4, o, m2);
    CHECK(melodyNotesIdentical(u1, u2));  // deterministic given the seed

    std::mt19937 m3(77u);
    const Melody u3 = mutate(prev, scale, { false, false }, 0.4, o, m3);
    CHECK(!melodyNotesIdentical(u1, u3));  // and varies with the seed
}

// When the two tracks differ in length, the LOCKED track is authoritative for
// note count, and pitches are read in order and then HELD at the last value —
// never cycled back to pitch 0 (the pre-4b artifact). Truncates when the pitch
// source is longer than the locked count.
void test_splice_count_matches_and_holds_last() {
    const Scale scale = minorPentatonic();
    MelodyOptions o;

    // Candidate SHORTER than the locked-rhythm previous: pitches run out.
    const Melody prev = melodyOfPitches({60, 61, 62, 63, 64, 65, 66, 67});  // 8
    const Melody cand = melodyOfPitches({70, 71, 72});                      // 3
    const Melody lr = recombineLocked(prev, cand, scale, { true, false }, o);

    CHECK(lr.notes.size() == prev.notes.size());   // locked count authoritative
    CHECK(lr.notes[0].noteNumber == 70);
    CHECK(lr.notes[1].noteNumber == 71);
    CHECK(lr.notes[2].noteNumber == 72);
    for (std::size_t i = 3; i < lr.notes.size(); ++i) {
        CHECK(lr.notes[i].noteNumber == 72);                       // last held
        CHECK(lr.notes[i].noteNumber != cand.notes[0].noteNumber); // NOT cycled
    }
    for (std::size_t i = 0; i < lr.notes.size(); ++i) {            // timing = locked
        CHECK(lr.notes[i].startBeats == prev.notes[i].startBeats);
        CHECK(lr.notes[i].lengthBeats == prev.notes[i].lengthBeats);
    }

    // Candidate LONGER than the locked previous: pitch track truncates.
    const Melody prev2 = melodyOfPitches({60, 61, 62});                  // 3
    const Melody cand2 = melodyOfPitches({70, 71, 72, 73, 74, 75});      // 6
    const Melody lr2 = recombineLocked(prev2, cand2, scale, { true, false }, o);
    CHECK(lr2.notes.size() == prev2.notes.size());  // 3
    CHECK(lr2.notes[0].noteNumber == 70);
    CHECK(lr2.notes[1].noteNumber == 71);
    CHECK(lr2.notes[2].noteNumber == 72);
}

// Mirrors the strong-beat predicate at MelodyGenerator.cpp:537: a beat position
// is strong iff it lands exactly on an integer beat on the tick grid. Verifies
// the integer test classifies integer beats as strong and half-beats as weak on
// the dyadic grid that current rhythm templates produce (multiples of 0.5).
void test_strong_beat_tick_grid() {
    constexpr long kTicksPerBeat = 960;  // must match MelodyGenerator.cpp
    auto isStrong = [](double localBeat) {
        const long tick = std::lround(localBeat * kTicksPerBeat);
        return (tick % kTicksPerBeat) == 0;
    };

    // Integer beats -> strong.
    for (double beat : {0.0, 1.0, 2.0, 3.0, 4.0})
        CHECK(isStrong(beat));

    // Half-beats (off-beats) -> weak.
    for (double beat : {0.5, 1.5, 2.5, 3.5})
        CHECK(!isStrong(beat));

    // Agrees with the old float test on the dyadic (0.5-multiple) grid: sweep
    // every half-beat from 0 to 8 and confirm identical classification.
    for (int half = 0; half <= 16; ++half) {
        const double beat = 0.5 * half;
        const bool oldStrong =
            std::fabs(beat - std::round(beat)) < 1e-3;
        CHECK(isStrong(beat) == oldStrong);
    }
}

}  // namespace

void run_melody_generator_tests() {
    test_strong_beat_tick_grid();
    test_random_walk_is_smooth();
    test_velocity_mapping_in_bytes();
    test_rhythm_modes();
    test_length_and_pure_random();
    test_reproducible();
    test_phrased_repeats_motif();
    test_phrased_cadence_on_tonic();
    test_phrased_rests_between_phrases();
    test_phrased_arpeggios_in_scale();
    test_phrased_dynamics_peak_mid_phrase();
    test_phrased_dynamics_are_smooth();
    test_phrased_ending_is_stepwise();
    test_cells_track_walk_freeform();
    test_cells_in_range_pure_random();
    test_cells_reproducible();
    test_cells_phrased_in_range_and_share_motif();
    test_arpeggiator_chord_tones_and_rate();
    test_arpeggiator_progression_moves();
    test_loop_fills_whole_bars();
    test_arpeggiator_reproducible();
    test_chords_are_diatonic_stacks();
    test_chords_spell_harmonic_minor_leading_tone();
    test_arpeggio_spells_harmonic_minor_leading_tone();
    test_arpeggio_blues_spells_in_scale_seventh();
    test_arpeggio_resolves_to_octave();
    test_phrased_tracks_brightness_at_high_influence();
    test_phrased_image_density();
    test_image_density_draws_no_rng();
    test_density_composes_melodic_content();
    test_phrased_syncopation_on_grid();
    test_energy_raises_velocity();
    test_repetition_repeats_motif();
    test_recombine_locks_dimensions();
    test_mutate_respects_locks();
    test_splice_locks_deterministic();
    test_splice_count_matches_and_holds_last();
}
