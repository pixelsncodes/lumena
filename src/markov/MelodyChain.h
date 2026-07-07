#pragma once

#include <cstddef>
#include <random>
#include <vector>

#include "markov/TheoryWeights.h"
#include "markov/TransitionMatrix.h"

namespace lumena::markov {

/// Samples a melody as a walk over scale degrees driven by a TransitionMatrix.
///
/// The base matrix is first-order; MelodyChain keeps one step of history (the
/// interval that led to the current degree, and the current repeat run) to
/// apply two dynamic voice-leading rules before sampling each step:
///   - leap resolution: after a leap larger than TheoryWeights::leapThreshold,
///     an opposite-direction step is strongly boosted;
///   - third-repeat damping: once a repeat has occurred, repeating again is
///     damped.
///
/// The RNG is supplied by the caller and never seeded internally, so a fixed
/// seed yields a reproducible melody. No method throws.
class MelodyChain {
public:
    /// Takes ownership of `matrix` (normalising it if needed) and borrows
    /// `rng`. `weights` supplies the dynamic-rule strengths (defaults if
    /// omitted).
    MelodyChain(TransitionMatrix matrix, std::mt19937& rng,
                TheoryWeights weights = TheoryWeights{});

    /// Samples the next degree from the current row's (dynamically adjusted)
    /// distribution.
    std::size_t next(std::size_t currentDegree);

    /// Samples the next degree from a blend of the Markov distribution and a
    /// pull toward `target` (a degree index; rounded and clamped).
    ///
    /// `bias` is clamped to [0,1]: 0 is pure Markov, 1 ignores Markov and lands
    /// exactly on the target. This is the hook for image brightness to steer
    /// the melody.
    std::size_t nextBiased(std::size_t currentDegree, float target, float bias);

    /// Forgets the second-order history (previous interval and repeat run).
    void reset() noexcept;

    std::size_t size() const noexcept { return matrix_.size(); }

private:
    // Fills work_ with the current degree's normalised distribution after
    // applying the dynamic voice-leading rules.
    void buildDynamicRow(std::size_t current);
    std::size_t sampleWork();
    void recordMove(std::size_t from, std::size_t to);

    TransitionMatrix matrix_;
    std::mt19937& rng_;
    TheoryWeights weights_;

    bool hasHistory_ = false;
    int arrivalInterval_ = 0;  // signed interval that produced the current degree
    int repeatRun_ = 0;        // consecutive repeats ending at the current degree

    std::vector<double> work_;  // reused per-step distribution buffer
};

} // namespace lumena::markov
