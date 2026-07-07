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

Scale minorPentatonic() {
    return Scale{"A Minor Pentatonic", 57, {0, 3, 5, 7, 10}};
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
        std::mt19937 rng(1u);
        const Melody melody = generateMelody(grid, scale, opts, rng);
        CHECK(melody.notes.size() == grid.cellCount());
    }

    // Explicit length is honoured, and PureRandom stays available as an option.
    {
        MelodyOptions opts;
        opts.length = 24;
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

}  // namespace

void run_melody_generator_tests() {
    test_random_walk_is_smooth();
    test_velocity_mapping_in_bytes();
    test_rhythm_modes();
    test_length_and_pure_random();
    test_reproducible();
}
