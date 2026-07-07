#include "melody/MelodyGenerator.h"

#include <algorithm>
#include <cmath>

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

    // The eight (dx, dy) offsets around a cell, in a fixed order.
    static constexpr int kDx[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    static constexpr int kDy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};

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

}  // namespace lumena::melody
