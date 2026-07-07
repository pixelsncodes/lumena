// Unit tests for the high-level melody generator: melodic smoothness (spatial
// coherence via the grid random walk + low brightness bias), brightness-driven
// velocity written into the MIDI byte stream, and Flowing/Straight rhythm.

#include <cstddef>
#include <cstdint>
#include <random>
#include <set>
#include <vector>

#include "image/BrightnessGrid.h"
#include "image/Image.h"
#include "melody/MelodyGenerator.h"
#include "midi/MidiFileWriter.h"
#include "midi/MidiSequence.h"
#include "scales/Scale.h"
#include "test_util.h"

namespace {

using lumena::image::BrightnessGrid;
using lumena::image::Image;
using lumena::melody::CellPath;
using lumena::melody::generateMelody;
using lumena::melody::kMaxVelocity;
using lumena::melody::kMinVelocity;
using lumena::melody::Melody;
using lumena::melody::MelodyOptions;
using lumena::melody::PhraseMode;
using lumena::melody::RhythmMode;
using lumena::midi::MidiFileWriter;
using lumena::midi::MidiSequence;
using lumena::midi::Note;
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

Scale minorPentatonic() {
    return Scale{"A Minor Pentatonic", 57, {0, 3, 5, 7, 10}};
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

}  // namespace

void run_melody_generator_tests() {
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
    test_phrased_ending_is_stepwise();
}
