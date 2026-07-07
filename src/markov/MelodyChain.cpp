#include "markov/MelodyChain.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lumena::markov {

MelodyChain::MelodyChain(TransitionMatrix matrix, std::mt19937& rng,
                         TheoryWeights weights)
    : matrix_(std::move(matrix)),
      rng_(rng),
      weights_(weights),
      work_(matrix_.size(), 0.0) {
    // Validate on use: a chain must sample from proper distributions.
    if (!matrix_.isNormalized()) {
        matrix_.normalize();
    }
}

void MelodyChain::reset() noexcept {
    hasHistory_ = false;
    arrivalInterval_ = 0;
    repeatRun_ = 0;
}

void MelodyChain::buildDynamicRow(std::size_t current) {
    const std::size_t n = matrix_.size();
    work_.assign(n, 0.0);
    if (n == 0) {
        return;
    }
    if (current >= n) {
        current = n - 1;
    }

    const double* row = matrix_.rowData(current);
    for (std::size_t j = 0; j < n; ++j) {
        work_[j] = row[j];
    }

    // (c) Leap resolution: after a leap wider than the threshold, boost a step
    // in the opposite direction (a downward step after an upward leap, etc.).
    if (hasHistory_ &&
        std::fabs(static_cast<double>(arrivalInterval_)) > weights_.leapThreshold) {
        const int resolveDir = (arrivalInterval_ > 0) ? -1 : +1;
        const long stepTarget = static_cast<long>(current) + resolveDir;
        if (stepTarget >= 0 && stepTarget < static_cast<long>(n)) {
            work_[static_cast<std::size_t>(stepTarget)] *=
                (1.0 + static_cast<double>(weights_.leapResolution));
        }
    }

    // (d) Third-repeat damping: if the previous move already repeated, damp
    // repeating a third time.
    if (repeatRun_ >= 1) {
        work_[current] *= static_cast<double>(weights_.thirdRepeatDamping);
    }

    // Renormalise.
    double sum = 0.0;
    for (double v : work_) {
        sum += v;
    }
    if (sum > 0.0) {
        for (double& v : work_) {
            v /= sum;
        }
    } else {
        const double uniform = 1.0 / static_cast<double>(n);
        for (double& v : work_) {
            v = uniform;
        }
    }
}

std::size_t MelodyChain::sampleWork() {
    const std::size_t n = work_.size();
    if (n == 0) {
        return 0;
    }
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    const double r = dist(rng_);
    double cumulative = 0.0;
    for (std::size_t j = 0; j < n; ++j) {
        cumulative += work_[j];
        if (r <= cumulative) {
            return j;
        }
    }
    // Floating-point slack: return the last degree carrying weight.
    for (std::size_t j = n; j-- > 0;) {
        if (work_[j] > 0.0) {
            return j;
        }
    }
    return n - 1;
}

void MelodyChain::recordMove(std::size_t from, std::size_t to) {
    const int interval = static_cast<int>(to) - static_cast<int>(from);
    arrivalInterval_ = interval;
    if (interval == 0) {
        ++repeatRun_;
    } else {
        repeatRun_ = 0;
    }
    hasHistory_ = true;
}

std::size_t MelodyChain::next(std::size_t currentDegree) {
    buildDynamicRow(currentDegree);
    const std::size_t chosen = sampleWork();
    recordMove(currentDegree, chosen);
    return chosen;
}

std::size_t MelodyChain::nextBiased(std::size_t currentDegree, float target,
                                    float bias) {
    buildDynamicRow(currentDegree);  // work_ = Markov distribution
    const std::size_t n = work_.size();
    if (n == 0) {
        return 0;
    }

    const double b = std::min(1.0, std::max(0.0, static_cast<double>(bias)));

    long targetDegree = std::lround(static_cast<double>(target));
    if (targetDegree < 0) {
        targetDegree = 0;
    } else if (targetDegree >= static_cast<long>(n)) {
        targetDegree = static_cast<long>(n) - 1;
    }

    // Blend: p = (1 - b) * markov + b * delta(target). Since the Markov row
    // already sums to 1, the result also sums to 1.
    for (double& v : work_) {
        v *= (1.0 - b);
    }
    work_[static_cast<std::size_t>(targetDegree)] += b;

    const std::size_t chosen = sampleWork();
    recordMove(currentDegree, chosen);
    return chosen;
}

} // namespace lumena::markov
