#pragma once

#include <cstddef>
#include <vector>

#include "markov/TheoryWeights.h"

namespace lumena::markov {

/// An N x N matrix of transition probabilities between scale degrees, where
/// N is the total number of degrees (scale degrees per octave x octave span,
/// matching Scale::noteAt). Every row is a probability distribution that sums
/// to 1.
///
/// The matrix is first-order; the history-dependent voice-leading rules
/// (leap resolution, third-repeat damping) are applied dynamically by
/// MelodyChain.
class TransitionMatrix {
public:
    /// Constructs an N x N zero matrix.
    explicit TransitionMatrix(std::size_t degrees);

    /// Builds a normalised matrix from music-theory rules: interval penalty,
    /// plus gravity toward the tonic degrees and the centre of the range.
    /// `degreesPerOctave` locates the tonic degrees (0, dpo, 2*dpo, ...).
    static TransitionMatrix fromTheory(std::size_t totalDegrees,
                                       std::size_t degreesPerOctave,
                                       const TheoryWeights& weights);

    std::size_t size() const noexcept { return degrees_; }

    /// Probability of moving from `from` to `to`; 0 if either index is out of
    /// range.
    double at(std::size_t from, std::size_t to) const noexcept;

    /// Sets an (unnormalised) weight; no-op if either index is out of range.
    void set(std::size_t from, std::size_t to, double value) noexcept;

    /// Pointer to the first element of row `from` (size() contiguous entries),
    /// or nullptr if out of range.
    const double* rowData(std::size_t from) const noexcept;

    /// Sum of row `from`; 0 if out of range.
    double rowSum(std::size_t from) const noexcept;

    /// Normalises every row to sum to 1. A row summing to <= 0 becomes uniform.
    void normalize() noexcept;

    /// True if every row sums to 1 within `eps` (vacuously true for size 0).
    bool isNormalized(double eps = 1e-9) const noexcept;

private:
    std::size_t index(std::size_t from, std::size_t to) const noexcept {
        return from * degrees_ + to;
    }

    std::size_t degrees_;
    std::vector<double> data_;  // row-major, degrees_ * degrees_
};

} // namespace lumena::markov
