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

#include <algorithm>
#include <cmath>
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
    // Phase 4b lock demos (all default off, so plain `--seed X` is unchanged):
    bool          lockRhythm = false;   // splice: keep the base timing track
    bool          lockPitch  = false;   // splice: keep the base pitch track
    long          regenSeed  = -1;      // >=0: recombineLocked against this seed
    double        mutateAmount = -1.0;  // >=0: mutate the result by this amount
    unsigned      mutateSeed = 1u;      // seed for the mutation draws
    // Coherence scoreboard (B-phrase plan §3). Read-only diagnostics computed
    // from the emitted note list; generation is untouched. All default off.
    bool          printMetrics = false;   // --metrics: print M1/M2/M3 for the run
    std::string   metricsCsvPath;         // --metrics-csv: write metric rows here
    int           sweep = 0;              // --sweep N: score seeds 1..N to the CSV
};

// ---- coherence scoreboard (M1 motif recall, M2 register drift, M3 interval
// entropy) — B-phrase fix plan §3. Everything below reads the emitted Melody
// only: no engine call, no RNG, no effect on generation or MIDI output. -----

// Interval class symbol for a signed semitone step: repeat / step / skip / leap,
// signed. Small alphabet so short per-bar sequences compare meaningfully.
char intervalClassSymbol(int d) {
    if (d == 0) return 'R';
    const int a = d > 0 ? d : -d;
    if (a <= 2) return d > 0 ? 'u' : 'd';
    if (a <= 4) return d > 0 ? 'S' : 's';
    return d > 0 ? 'L' : 'l';
}

// Normalized Levenshtein distance between two symbol strings, in [0, 1].
// Both empty -> 0 (identical); exactly one empty -> 1 (nothing shared).
double normalizedEditDistance(const std::string& a, const std::string& b) {
    if (a.empty() && b.empty()) return 0.0;
    if (a.empty() || b.empty()) return 1.0;
    const std::size_t n = a.size(), m = b.size();
    std::vector<std::size_t> prev(m + 1), cur(m + 1);
    for (std::size_t j = 0; j <= m; ++j) prev[j] = j;
    for (std::size_t i = 1; i <= n; ++i) {
        cur[0] = i;
        for (std::size_t j = 1; j <= m; ++j) {
            const std::size_t sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return static_cast<double>(prev[m]) /
           static_cast<double>(std::max(n, m));
}

// The interval-class symbol sequence over notes [begin, end) of the melody
// (consecutive-note pitch steps, in emitted order).
std::string symbolsForRange(const std::vector<Note>& notes, std::size_t begin,
                            std::size_t end) {
    std::string syms;
    for (std::size_t i = begin; i + 1 < end; ++i)
        syms.push_back(
            intervalClassSymbol(notes[i + 1].noteNumber - notes[i].noteNumber));
    return syms;
}

// Shannon entropy (bits) of the symbol distribution of `syms`.
double symbolEntropy(const std::string& syms) {
    if (syms.empty()) return 0.0;
    double counts[128] = {0.0};
    for (char c : syms) counts[static_cast<unsigned char>(c)] += 1.0;
    const double total = static_cast<double>(syms.size());
    double h = 0.0;
    for (double c : counts) {
        if (c <= 0.0) continue;
        const double p = c / total;
        h -= p * std::log2(p);
    }
    return h;
}

struct CoherenceMetrics {
    // M1 motif recall: normalized edit distance of interval-class sequences.
    double m1Front = -1.0;   // mean dist(bar 1, bar k), k in bars 2-4
    double m1Back = -1.0;    // mean dist(bar 1, bar k), k in bars 5-8
    double m1APrime = -1.0;  // mean dist(motif A, A-family phrase), phrased only
    double m1B = -1.0;       // mean dist(motif A, B phrase), phrased only
    // M2 register drift: max back-half excursion (semitones) of the per-bar
    // mean pitch outside the front-half centroid band.
    double m2Excursion = -1.0;
    // M3 interval entropy over a sliding 2-bar window: back minus front.
    double m3Front = -1.0;
    double m3Back = -1.0;
    double m3Delta = 0.0;
    // Supplementary register decomposition (S-4 diagnostic): mean |phrase pitch
    // centroid - motif A's centroid| in semitones, split by phrase role. Reads
    // the register relatedness C-1 targets directly, without M2's front-band
    // artifact (the band itself contains an early B phrase).
    double regAPrime = -1.0;
    double regB = -1.0;
    int bars = 0;
};

// Computes the plan-§3 scoreboard from the emitted note list. Front half =
// bars 1-4, back half = bars 5-8 (the 8-bar audition protocol; a 9th overshoot
// bar is ignored). M1's front mean excludes bar 1's zero self-distance.
CoherenceMetrics computeCoherenceMetrics(const Melody& melody,
                                         double beatsPerBar) {
    CoherenceMetrics m;
    const std::vector<Note>& notes = melody.notes;
    if (notes.empty() || beatsPerBar <= 0.0) return m;

    // Bucket note indices by bar (notes are emitted in start order).
    double lastEnd = 0.0;
    for (const Note& n : notes)
        lastEnd = std::max(lastEnd, n.startBeats + n.lengthBeats);
    const int barCount = static_cast<int>(std::ceil(lastEnd / beatsPerBar));
    m.bars = barCount;
    std::vector<std::vector<std::size_t>> barNotes(
        static_cast<std::size_t>(std::max(barCount, 1)));
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const int bar = static_cast<int>(notes[i].startBeats / beatsPerBar);
        if (bar >= 0 && bar < barCount)
            barNotes[static_cast<std::size_t>(bar)].push_back(i);
    }

    // Per-bar interval-class symbols (steps between consecutive notes that
    // START in the bar) and per-bar pitch centroids.
    std::vector<std::string> barSyms(barNotes.size());
    std::vector<double> centroid(barNotes.size(), -1.0);
    for (std::size_t b = 0; b < barNotes.size(); ++b) {
        const std::vector<std::size_t>& idx = barNotes[b];
        std::string syms;
        for (std::size_t k = 0; k + 1 < idx.size(); ++k)
            syms.push_back(intervalClassSymbol(notes[idx[k + 1]].noteNumber -
                                               notes[idx[k]].noteNumber));
        barSyms[b] = syms;
        if (!idx.empty()) {
            double sum = 0.0;
            for (std::size_t i : idx) sum += notes[i].noteNumber;
            centroid[b] = sum / static_cast<double>(idx.size());
        }
    }

    const std::size_t frontEnd = 4;  // bars 1-4 -> indices 0-3
    const std::size_t backEnd = 8;   // bars 5-8 -> indices 4-7

    // M1 per-bar: distance of each bar's symbols from bar 1's.
    double frontSum = 0.0, backSum = 0.0;
    int frontN = 0, backN = 0;
    for (std::size_t b = 1; b < std::min(backEnd, barSyms.size()); ++b) {
        const double d = normalizedEditDistance(barSyms[0], barSyms[b]);
        if (b < frontEnd) { frontSum += d; ++frontN; }
        else              { backSum += d; ++backN; }
    }
    if (frontN > 0) m.m1Front = frontSum / frontN;
    if (backN > 0) m.m1Back = backSum / backN;

    // M1 per-phrase: distance of each body phrase from motif A (phrase 0).
    // Phrase roles by construction (generatePhrased): index 0 = A, last =
    // closing, interior odd = A-family variation slot, interior even = B.
    const std::vector<std::size_t>& starts = melody.phraseStarts;
    if (starts.size() >= 3) {
        std::vector<std::string> phraseSyms(starts.size());
        for (std::size_t p = 0; p < starts.size(); ++p) {
            const std::size_t begin = starts[p];
            const std::size_t end =
                p + 1 < starts.size() ? starts[p + 1] : notes.size();
            phraseSyms[p] = symbolsForRange(notes, begin, end);
        }
        double aSum = 0.0, bSum = 0.0;
        int aN = 0, bN = 0;
        // Per-phrase pitch centroids for the register decomposition.
        std::vector<double> phraseCentroid(starts.size(), -1.0);
        for (std::size_t p = 0; p < starts.size(); ++p) {
            const std::size_t begin = starts[p];
            const std::size_t end =
                p + 1 < starts.size() ? starts[p + 1] : notes.size();
            if (end <= begin) continue;
            double sum = 0.0;
            for (std::size_t i = begin; i < end; ++i)
                sum += notes[i].noteNumber;
            phraseCentroid[p] = sum / static_cast<double>(end - begin);
        }
        double aReg = 0.0, bReg = 0.0;
        int aRegN = 0, bRegN = 0;
        for (std::size_t p = 1; p + 1 < starts.size(); ++p) {
            const double d = normalizedEditDistance(phraseSyms[0], phraseSyms[p]);
            if (p % 2 == 1) { aSum += d; ++aN; }
            else            { bSum += d; ++bN; }
            if (phraseCentroid[p] >= 0.0 && phraseCentroid[0] >= 0.0) {
                const double rd = std::fabs(phraseCentroid[p] - phraseCentroid[0]);
                if (p % 2 == 1) { aReg += rd; ++aRegN; }
                else            { bReg += rd; ++bRegN; }
            }
        }
        if (aN > 0) m.m1APrime = aSum / aN;
        if (bN > 0) m.m1B = bSum / bN;
        if (aRegN > 0) m.regAPrime = aReg / aRegN;
        if (bRegN > 0) m.regB = bReg / bRegN;
    }

    // M2: front-half centroid band, max back-half excursion outside it.
    double bandLo = 1e9, bandHi = -1e9;
    for (std::size_t b = 0; b < std::min(frontEnd, centroid.size()); ++b) {
        if (centroid[b] < 0.0) continue;
        bandLo = std::min(bandLo, centroid[b]);
        bandHi = std::max(bandHi, centroid[b]);
    }
    if (bandHi >= bandLo) {
        double worst = 0.0;
        bool any = false;
        for (std::size_t b = frontEnd; b < std::min(backEnd, centroid.size());
             ++b) {
            if (centroid[b] < 0.0) continue;
            any = true;
            double ex = 0.0;
            if (centroid[b] > bandHi) ex = centroid[b] - bandHi;
            if (centroid[b] < bandLo) ex = bandLo - centroid[b];
            worst = std::max(worst, ex);
        }
        if (any) m.m2Excursion = worst;
    }

    // M3: entropy over sliding 2-bar windows; front windows live entirely in
    // bars 1-4, back windows entirely in bars 5-8.
    double f3 = 0.0, b3 = 0.0;
    int f3N = 0, b3N = 0;
    for (std::size_t b = 0; b + 1 < std::min(backEnd, barSyms.size()); ++b) {
        const std::string window = barSyms[b] + barSyms[b + 1];
        if (window.empty()) continue;
        const double h = symbolEntropy(window);
        if (b + 1 < frontEnd) { f3 += h; ++f3N; }
        else if (b >= frontEnd) { b3 += h; ++b3N; }
    }
    if (f3N > 0) m.m3Front = f3 / f3N;
    if (b3N > 0) m.m3Back = b3 / b3N;
    if (f3N > 0 && b3N > 0) m.m3Delta = m.m3Back - m.m3Front;
    return m;
}

const char* kMetricsCsvHeader =
    "seed,notes,bars,m1_front,m1_back,m1_aprime,m1_b,"
    "m2_excursion,m3_front,m3_back,m3_delta,reg_aprime,reg_b\n";

void writeMetricsCsvRow(std::FILE* f, unsigned seed, std::size_t noteCount,
                        const CoherenceMetrics& m) {
    std::fprintf(f,
                 "%u,%zu,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
                 seed, noteCount, m.bars, m.m1Front, m.m1Back, m.m1APrime,
                 m.m1B, m.m2Excursion, m.m3Front, m.m3Back, m.m3Delta,
                 m.regAPrime, m.regB);
}

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
                 "          [--image-influence 0..1] [--repetition 0..1]\n"
                 "          [--density 0..1]\n"
                 "          [--metrics] [--metrics-csv file.csv] [--sweep N]\n",
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
        } else if (arg == "--density" && i + 1 < argc) {
            opts.melody.imageRhythmAmount = std::strtod(argv[++i], nullptr);
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
        } else if (arg == "--progression" && i + 1 < argc) {
            // Explicit chord progression (Lock Harmony): comma-separated diatonic
            // root degrees, e.g. "0,4,5,3" for I-V-vi-IV. Same list + different
            // --seed => same harmony, re-rolled pitch/rhythm.
            opts.melody.progression.clear();
            const char* p = argv[++i];
            while (*p) {
                char* end = nullptr;
                const long v = std::strtol(p, &end, 10);
                if (end == p) break;
                opts.melody.progression.push_back(static_cast<int>(v));
                p = (*end == ',') ? end + 1 : end;
            }
        } else if (arg == "--lock" && i + 1 < argc) {
            const std::string d = argv[++i];
            if (d == "rhythm") opts.lockRhythm = true;
            else if (d == "pitch") opts.lockPitch = true;
            else { std::fprintf(stderr, "Unknown --lock: %s\n", d.c_str()); return false; }
        } else if (arg == "--regen-seed" && i + 1 < argc) {
            opts.regenSeed = std::strtol(argv[++i], nullptr, 10);
        } else if (arg == "--mutate" && i + 1 < argc) {
            opts.mutateAmount = std::strtod(argv[++i], nullptr);
        } else if (arg == "--mutate-seed" && i + 1 < argc) {
            opts.mutateSeed = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 10));
        } else if (arg == "--metrics") {
            opts.printMetrics = true;
        } else if (arg == "--metrics-csv" && i + 1 < argc) {
            opts.metricsCsvPath = argv[++i];
        } else if (arg == "--sweep" && i + 1 < argc) {
            opts.sweep = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
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

    // Coherence sweep (plan §3 protocol): score seeds 1..N into a CSV and exit.
    // Each iteration seeds a fresh rng exactly as a standalone run would, so
    // sweep row k is byte-identical to a single `--seed k` run's metrics.
    if (opts.sweep > 0) {
        if (opts.metricsCsvPath.empty()) {
            std::fprintf(stderr, "--sweep requires --metrics-csv\n");
            return 2;
        }
        std::FILE* csv = std::fopen(opts.metricsCsvPath.c_str(), "wb");
        if (!csv) {
            std::fprintf(stderr, "Failed to open CSV file for writing: %s\n",
                         opts.metricsCsvPath.c_str());
            return 1;
        }
        std::fprintf(csv, "%s", kMetricsCsvHeader);
        for (int s = 1; s <= opts.sweep; ++s) {
            std::mt19937 sweepRng(static_cast<unsigned>(s));
            const Melody swept = lumena::melody::generateMelody(
                grid, detection.scale, opts.melody, sweepRng);
            const CoherenceMetrics cm = computeCoherenceMetrics(
                swept, opts.melody.beatsPerBar > 0.0 ? opts.melody.beatsPerBar
                                                     : 4.0);
            writeMetricsCsvRow(csv, static_cast<unsigned>(s),
                               swept.notes.size(), cm);
        }
        std::fclose(csv);
        std::printf("Wrote metrics CSV: %s (%d seeds)\n",
                    opts.metricsCsvPath.c_str(), opts.sweep);
        return 0;
    }

    std::mt19937 rng(opts.seed);
    Melody melody =
        lumena::melody::generateMelody(grid, detection.scale, opts.melody, rng);

    // Phase 4b splice locks: regenerate a candidate under a second seed and keep
    // the locked track from the first melody (Lock Rhythm -> new pitches, etc.).
    if (opts.regenSeed >= 0 && (opts.lockRhythm || opts.lockPitch)) {
        std::mt19937 rng2(static_cast<unsigned>(opts.regenSeed));
        const Melody cand =
            lumena::melody::generateMelody(grid, detection.scale, opts.melody, rng2);
        lumena::melody::RegenLocks locks;
        locks.rhythm = opts.lockRhythm;
        locks.pitch = opts.lockPitch;
        melody = lumena::melody::recombineLocked(melody, cand, detection.scale,
                                                 locks, opts.melody);
        std::printf("Recombined: lock %s against seed %ld\n",
                    opts.lockRhythm ? "rhythm" : "pitch", opts.regenSeed);
    }

    // Phase 4b mutate: nudge the result (honouring any lock) for a variation.
    if (opts.mutateAmount >= 0.0) {
        std::mt19937 mrng(opts.mutateSeed);
        lumena::melody::RegenLocks locks;
        locks.rhythm = opts.lockRhythm;
        locks.pitch = opts.lockPitch;
        melody = lumena::melody::mutate(melody, detection.scale, locks,
                                        opts.mutateAmount, opts.melody, mrng);
        std::printf("Mutated by %.2f (seed %u)\n", opts.mutateAmount, opts.mutateSeed);
    }

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

    // Coherence scoreboard for this single run (read-only; plan §3).
    if (opts.printMetrics || !opts.metricsCsvPath.empty()) {
        const CoherenceMetrics cm = computeCoherenceMetrics(
            melody,
            opts.melody.beatsPerBar > 0.0 ? opts.melody.beatsPerBar : 4.0);
        if (opts.printMetrics) {
            std::printf(
                "Metrics (seed %u): M1 front %.3f back %.3f | dist(A') %.3f "
                "dist(B) %.3f | M2 excursion %.2f st | M3 front %.3f back %.3f "
                "delta %+.3f | %d bars\n",
                opts.seed, cm.m1Front, cm.m1Back, cm.m1APrime, cm.m1B,
                cm.m2Excursion, cm.m3Front, cm.m3Back, cm.m3Delta, cm.bars);
        }
        if (!opts.metricsCsvPath.empty()) {
            std::FILE* csv = std::fopen(opts.metricsCsvPath.c_str(), "wb");
            if (!csv) {
                std::fprintf(stderr, "Failed to open CSV file for writing: %s\n",
                             opts.metricsCsvPath.c_str());
                return 1;
            }
            std::fprintf(csv, "%s", kMetricsCsvHeader);
            writeMetricsCsvRow(csv, opts.seed, melody.notes.size(), cm);
            std::fclose(csv);
            std::printf("Wrote metrics CSV: %s\n", opts.metricsCsvPath.c_str());
        }
    }

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
                     "source_brightness,chord_tone,"
                     // Appended bug-4 diagnostic columns (Phrased Melody only;
                     // -1/0 elsewhere). realStartBeat duplicates startBeat so the
                     // snap-site metric is self-contained on the appended block.
                     "realStartBeat,was_strong,was_snapped,target_chord_root,"
                     // Phase 4.5: the phrase the note belongs to (from
                     // Melody::phraseStarts; -1 outside Phrased mode), so the
                     // invariant harness can assert the A A' B A'' form.
                     "phrase\n");
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
            // Chord-tone role (0=root,1=third,...); -1 for melodic notes, which
            // do not populate the chordTones track.
            const int chordTone =
                i < melody.chordTones.size() ? melody.chordTones[i] : -1;
            // Bug-4 diagnostics: 0/-1 when the track is empty (non-Phrased modes).
            const int wasStrong =
                i < melody.dbgStrong.size() ? melody.dbgStrong[i] : 0;
            const int wasSnapped =
                i < melody.dbgSnapped.size() ? melody.dbgSnapped[i] : 0;
            const int chordRoot =
                i < melody.dbgChordRoot.size() ? melody.dbgChordRoot[i] : -1;
            int phraseIdx = -1;
            for (std::size_t p = 0; p < melody.phraseStarts.size(); ++p) {
                if (melody.phraseStarts[p] <= i) phraseIdx = static_cast<int>(p);
            }
            std::fprintf(csv, "%zu,%d,%.6f,%.6f,%d,%d,%.6f,%d,%.6f,%d,%d,%d,%d\n",
                         i, n.noteNumber, n.startBeats, n.lengthBeats, n.velocity,
                         degree, srcBrightness, chordTone,
                         n.startBeats, wasStrong, wasSnapped, chordRoot,
                         phraseIdx);
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
