// Lumena demo: turn an image into a melody and print it, optionally writing a
// Standard MIDI File.
//
// Usage:
//   lumena_demo <image> [--out melody.mid] [--seed N] [--tempo BPM]
//
// It runs the full standalone pipeline end to end — image -> brightness grid ->
// circle-of-fifths key -> theory-weighted Markov walk -> MIDI notes — so it
// doubles as a smoke test for the whole library. The generated file opens in
// any DAW or MIDI player.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "image/BrightnessGrid.h"
#include "image/Image.h"
#include "markov/MelodyChain.h"
#include "markov/TheoryWeights.h"
#include "markov/TransitionMatrix.h"
#include "midi/MidiFileWriter.h"
#include "midi/MidiSequence.h"
#include "scales/KeySelector.h"
#include "scales/Scale.h"
#include "scales/ScaleLibrary.h"

namespace {

using lumena::image::BrightnessGrid;
using lumena::image::Image;
using lumena::markov::MelodyChain;
using lumena::markov::TheoryWeights;
using lumena::markov::TransitionMatrix;
using lumena::midi::MidiFileWriter;
using lumena::midi::MidiSequence;
using lumena::midi::Note;
using lumena::scales::KeyDetection;
using lumena::scales::KeySelector;
using lumena::scales::mapBrightnessToDegree;
using lumena::scales::Scale;

struct Options {
    std::string imagePath;
    std::string outPath;      // empty -> do not write a file
    unsigned    seed  = 20260707u;
    double      tempo = 120.0;
    int         octaveSpan = 2;
};

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                 "Usage: %s <image> [--out melody.mid] [--seed N] "
                 "[--tempo BPM]\n",
                 argv0);
}

bool parseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            opts.outPath = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            opts.seed = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--tempo" && i + 1 < argc) {
            opts.tempo = std::strtod(argv[++i], nullptr);
        } else if (arg == "-h" || arg == "--help") {
            return false;
        } else if (!arg.empty() && arg[0] == '-') {
            std::fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            return false;
        } else if (opts.imagePath.empty()) {
            opts.imagePath = arg;
        } else {
            std::fprintf(stderr, "Unexpected argument: %s\n", arg.c_str());
            return false;
        }
    }
    return !opts.imagePath.empty();
}

// Walks the brightness grid, steering a theory-weighted Markov chain toward the
// scale degree each cell's brightness suggests, and emits one quarter note per
// cell.
std::vector<Note> generateMelody(const BrightnessGrid& grid, const Scale& scale,
                                 int octaveSpan, unsigned seed) {
    const TheoryWeights weights;
    const std::size_t degreesPerOctave = scale.degreesPerOctave();
    const int totalDegrees = scale.usableDegrees(octaveSpan);

    std::vector<Note> notes;
    if (totalDegrees <= 0 || degreesPerOctave == 0) {
        return notes;
    }

    const TransitionMatrix matrix = TransitionMatrix::fromTheory(
        static_cast<std::size_t>(totalDegrees), degreesPerOctave, weights);
    std::mt19937 rng(seed);
    MelodyChain chain(matrix, rng, weights);

    // Start in the middle of the range so the walk has room in both directions.
    std::size_t degree = static_cast<std::size_t>(totalDegrees / 2);

    const std::vector<float>& cells = grid.values();
    notes.reserve(cells.size());
    double beat = 0.0;
    for (float brightness : cells) {
        const int target = mapBrightnessToDegree(brightness, totalDegrees);
        // Blend the Markov motion with the image's suggested degree.
        degree = chain.nextBiased(degree, static_cast<float>(target), 0.5f);

        Note note;
        note.noteNumber = scale.noteAt(static_cast<int>(degree), octaveSpan);
        note.velocity = 96;
        note.startBeats = beat;
        note.lengthBeats = 1.0;
        notes.push_back(note);
        beat += 1.0;
    }
    return notes;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        printUsage(argv[0]);
        return 2;
    }

    const auto image = Image::loadFromFile(opts.imagePath);
    if (!image || image->empty()) {
        std::fprintf(stderr, "Failed to load image: %s\n",
                     opts.imagePath.c_str());
        return 1;
    }
    std::printf("Loaded %s (%dx%d)\n", opts.imagePath.c_str(), image->width(),
                image->height());

    const BrightnessGrid grid(*image, 16, 12);

    const KeySelector selector;
    const KeyDetection detection = selector.detect(*image);
    std::printf("Detected key: %s (hue %.0f°, saturation %.2f)\n",
                detection.keyName.c_str(), detection.hue, detection.saturation);

    const std::vector<Note> notes =
        generateMelody(grid, detection.scale, opts.octaveSpan, opts.seed);
    std::printf("Generated %zu notes at %.0f BPM\n", notes.size(), opts.tempo);

    const MidiSequence sequence(notes, opts.tempo, MidiSequence::kDefaultPpq);

    if (!opts.outPath.empty()) {
        if (MidiFileWriter::write(sequence, opts.outPath)) {
            std::printf("Wrote MIDI file: %s (%zu events)\n",
                        opts.outPath.c_str(), sequence.size());
        } else {
            std::fprintf(stderr, "Failed to write MIDI file: %s\n",
                         opts.outPath.c_str());
            return 1;
        }
    }

    return 0;
}
