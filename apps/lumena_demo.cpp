// Lumena demo: turn an image into a melody and print it, optionally writing a
// Standard MIDI File.
//
// Usage:
//   lumena_demo <image> [--out melody.mid] [--seed N] [--tempo BPM]
//                       [--rhythm straight|flowing] [--length N]
//                       [--cells walk|random] [--mode phrased|freeform]
//                       [--arp 0..1] [--dump-notes notes.csv]
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
#include "melody/MelodyGenerator.h"
#include "midi/MidiFileWriter.h"
#include "midi/MidiSequence.h"
#include "scales/KeySelector.h"

namespace {

using lumena::image::BrightnessGrid;
using lumena::image::Image;
using lumena::melody::ArpPattern;
using lumena::melody::CellPath;
using lumena::melody::GenerationMode;
using lumena::melody::Melody;
using lumena::melody::MelodyOptions;
using lumena::melody::PhraseMode;
using lumena::melody::RhythmMode;
using lumena::midi::MidiFileWriter;
using lumena::midi::MidiSequence;
using lumena::midi::Note;
using lumena::scales::KeyDetection;
using lumena::scales::KeySelector;

struct Options {
    std::string   imagePath;
    std::string   outPath;      // empty -> do not write a file
    std::string   dumpNotesPath;  // empty -> do not write a CSV of the notes
    unsigned      seed  = 20260707u;
    double        tempo = 120.0;
    MelodyOptions melody;       // rhythm/cells/length/bias default to musical values
};

void printUsage(const char* argv0) {
    std::fprintf(stderr,
                 "Usage: %s <image> [--out melody.mid] [--dump-notes notes.csv]\n"
                 "          [--seed N] [--tempo BPM]\n"
                 "          [--rhythm straight|flowing] [--length N] "
                 "[--cells walk|random]\n"
                 "          [--mode phrased|freeform]\n"
                 "          [--arpeggiate | --chords [--chord-size N]]\n"
                 "          [--arp-pattern up|down|updown|converge|random]\n"
                 "          [--loop-bars 0|1|2|4|8]\n"
                 "          [--energy 0..1] [--complexity 0..1]\n"
                 "          [--image-influence 0..1] [--repetition 0..1]\n",
                 argv0);
}

bool parseArgs(int argc, char** argv, Options& opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            opts.outPath = argv[++i];
        } else if (arg == "--dump-notes" && i + 1 < argc) {
            opts.dumpNotesPath = argv[++i];
        } else if (arg == "--seed" && i + 1 < argc) {
            opts.seed = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--tempo" && i + 1 < argc) {
            opts.tempo = std::strtod(argv[++i], nullptr);
        } else if (arg == "--length" && i + 1 < argc) {
            opts.melody.length = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        } else if (arg == "--rhythm" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode == "straight") {
                opts.melody.rhythm = RhythmMode::Straight;
            } else if (mode == "flowing") {
                opts.melody.rhythm = RhythmMode::Flowing;
            } else {
                std::fprintf(stderr, "Unknown --rhythm mode: %s\n", mode.c_str());
                return false;
            }
        } else if (arg == "--cells" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode == "walk") {
                opts.melody.cellPath = CellPath::RandomWalk;
            } else if (mode == "random") {
                opts.melody.cellPath = CellPath::PureRandom;
            } else {
                std::fprintf(stderr, "Unknown --cells mode: %s\n", mode.c_str());
                return false;
            }
        } else if (arg == "--mode" && i + 1 < argc) {
            const std::string mode = argv[++i];
            if (mode == "phrased") {
                opts.melody.phraseMode = PhraseMode::Phrased;
            } else if (mode == "freeform") {
                opts.melody.phraseMode = PhraseMode::Freeform;
            } else {
                std::fprintf(stderr, "Unknown --mode: %s\n", mode.c_str());
                return false;
            }
        } else if (arg == "--arp" && i + 1 < argc) {
            opts.melody.arpeggioAmount = std::strtod(argv[++i], nullptr);
        } else if (arg == "--arpeggiate") {
            opts.melody.mode = GenerationMode::Arpeggio;
        } else if (arg == "--chords") {
            opts.melody.mode = GenerationMode::Chords;
        } else if (arg == "--chord-size" && i + 1 < argc) {
            opts.melody.chordSize =
                static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        } else if (arg == "--energy" && i + 1 < argc) {
            opts.melody.energy = std::strtod(argv[++i], nullptr);
        } else if (arg == "--complexity" && i + 1 < argc) {
            opts.melody.arpeggioAmount = std::strtod(argv[++i], nullptr);
        } else if (arg == "--image-influence" && i + 1 < argc) {
            opts.melody.brightnessBias = std::strtod(argv[++i], nullptr);
        } else if (arg == "--repetition" && i + 1 < argc) {
            opts.melody.repetition = std::strtod(argv[++i], nullptr);
        } else if (arg == "--arp-octaves" && i + 1 < argc) {
            opts.melody.arpOctaves =
                static_cast<int>(std::strtol(argv[++i], nullptr, 10));
        } else if (arg == "--arp-pattern" && i + 1 < argc) {
            const std::string p = argv[++i];
            if (p == "up") {
                opts.melody.arpPattern = ArpPattern::Up;
            } else if (p == "down") {
                opts.melody.arpPattern = ArpPattern::Down;
            } else if (p == "updown") {
                opts.melody.arpPattern = ArpPattern::UpDown;
            } else if (p == "converge") {
                opts.melody.arpPattern = ArpPattern::Converge;
            } else if (p == "random") {
                opts.melody.arpPattern = ArpPattern::Random;
            } else {
                std::fprintf(stderr, "Unknown --arp-pattern: %s\n", p.c_str());
                return false;
            }
        } else if (arg == "--loop-bars" && i + 1 < argc) {
            opts.melody.loopBars =
                static_cast<int>(std::strtol(argv[++i], nullptr, 10));
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

    std::mt19937 rng(opts.seed);
    const Melody melody =
        lumena::melody::generateMelody(grid, detection.scale, opts.melody, rng);
    const std::vector<Note>& notes = melody.notes;
    const char* structure =
        opts.melody.mode == GenerationMode::Arpeggio
            ? "arpeggiated"
            : opts.melody.mode == GenerationMode::Chords
                  ? "chords"
                  : (opts.melody.phraseMode == PhraseMode::Phrased ? "phrased"
                                                                   : "freeform");
    std::printf("Generated %zu notes at %.0f BPM (%s%s, %s rhythm, %s cells)\n",
                notes.size(), opts.tempo, structure,
                opts.melody.loopBars > 0 ? ", looped" : "",
                opts.melody.rhythm == RhythmMode::Flowing ? "flowing" : "straight",
                opts.melody.cellPath == CellPath::RandomWalk ? "walk" : "random");

    // Demo-only diagnostics: dump the raw generated notes as CSV so external
    // tooling can measure how the melody varies across seeds and images. This
    // reads the already-generated Melody and does not touch generation.
    if (!opts.dumpNotesPath.empty()) {
        std::FILE* csv = std::fopen(opts.dumpNotesPath.c_str(), "wb");
        if (!csv) {
            std::fprintf(stderr, "Failed to open CSV file for writing: %s\n",
                         opts.dumpNotesPath.c_str());
            return 1;
        }
        std::fprintf(csv,
                     "index,pitch,startBeat,durationBeats,velocity,degree,"
                     "source_brightness\n");
        for (std::size_t i = 0; i < notes.size(); ++i) {
            const Note& n = notes[i];
            const int degree =
                i < melody.degrees.size() ? melody.degrees[i] : -1;
            // Source-cell brightness for the note's provenance cell, read back
            // from the same grid the generator used. -1 flags a missing cell.
            double srcBrightness = -1.0;
            if (i < melody.cells.size()) {
                const auto& cell = melody.cells[i];
                srcBrightness = grid.valueAt(cell.col, cell.row);
            }
            std::fprintf(csv, "%zu,%d,%.6f,%.6f,%d,%d,%.6f\n", i, n.noteNumber,
                         n.startBeats, n.lengthBeats, n.velocity, degree,
                         srcBrightness);
        }
        std::fclose(csv);
        std::printf("Wrote notes CSV: %s (%zu rows)\n",
                    opts.dumpNotesPath.c_str(), notes.size());
    }

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
