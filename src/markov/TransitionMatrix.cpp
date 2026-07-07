#include "markov/TransitionMatrix.h"

#include <algorithm>
#include <cmath>

namespace lumena::markov {

namespace {

// Proximity in [0,1] of degree `j` to the nearest tonic (a multiple of
// `degreesPerOctave`): 1 on a tonic, decaying with scale-step distance.
double tonicProximity(std::size_t j, std::size_t degreesPerOctave) {
    if (degreesPerOctave == 0) {
        return j == 0 ? 1.0 : 0.0;
    }
    const std::size_t mod = j % degreesPerOctave;
    const std::size_t dist = std::min(mod, degreesPerOctave - mod);
    return 1.0 / (1.0 + static_cast<double>(dist));
}

// Proximity in [0,1] of degree `j` to the centre of the range: 1 at centre,
// 0 at the extremes.
double centerProximity(std::size_t j, double center) {
    if (center <= 0.0) {
        return 1.0;
    }
    const double d = std::fabs(static_cast<double>(j) - center) / center;
    return std::max(0.0, 1.0 - d);
}

}  // namespace

TransitionMatrix::TransitionMatrix(std::size_t degrees)
    : degrees_(degrees), data_(degrees * degrees, 0.0) {}

double TransitionMatrix::at(std::size_t from, std::size_t to) const noexcept {
    if (from >= degrees_ || to >= degrees_) {
        return 0.0;
    }
    return data_[index(from, to)];
}

void TransitionMatrix::set(std::size_t from, std::size_t to,
                           double value) noexcept {
    if (from >= degrees_ || to >= degrees_) {
        return;
    }
    data_[index(from, to)] = value;
}

const double* TransitionMatrix::rowData(std::size_t from) const noexcept {
    if (from >= degrees_) {
        return nullptr;
    }
    return &data_[index(from, 0)];
}

double TransitionMatrix::rowSum(std::size_t from) const noexcept {
    if (from >= degrees_) {
        return 0.0;
    }
    double sum = 0.0;
    const std::size_t base = index(from, 0);
    for (std::size_t j = 0; j < degrees_; ++j) {
        sum += data_[base + j];
    }
    return sum;
}

void TransitionMatrix::normalize() noexcept {
    for (std::size_t i = 0; i < degrees_; ++i) {
        const std::size_t base = index(i, 0);
        double sum = 0.0;
        for (std::size_t j = 0; j < degrees_; ++j) {
            sum += data_[base + j];
        }
        if (sum > 0.0) {
            for (std::size_t j = 0; j < degrees_; ++j) {
                data_[base + j] /= sum;
            }
        } else {
            const double uniform = 1.0 / static_cast<double>(degrees_);
            for (std::size_t j = 0; j < degrees_; ++j) {
                data_[base + j] = uniform;
            }
        }
    }
}

bool TransitionMatrix::isNormalized(double eps) const noexcept {
    for (std::size_t i = 0; i < degrees_; ++i) {
        if (std::fabs(rowSum(i) - 1.0) > eps) {
            return false;
        }
    }
    return true;
}

TransitionMatrix TransitionMatrix::fromTheory(std::size_t totalDegrees,
                                              std::size_t degreesPerOctave,
                                              const TheoryWeights& weights) {
    TransitionMatrix matrix(totalDegrees);
    if (totalDegrees == 0) {
        return matrix;
    }

    const double center = (static_cast<double>(totalDegrees) - 1.0) / 2.0;

    for (std::size_t i = 0; i < totalDegrees; ++i) {
        for (std::size_t j = 0; j < totalDegrees; ++j) {
            const std::size_t dist = (i > j) ? (i - j) : (j - i);

            // (a) interval penalty
            double intervalWeight;
            if (dist == 0) {
                intervalWeight = weights.repeatWeight;  // (d) repeats allowed
            } else {
                intervalWeight = std::pow(static_cast<double>(weights.intervalDecay),
                                          static_cast<double>(dist - 1));
            }

            // (b) gravity toward tonic and centre
            const double gravity =
                1.0 +
                static_cast<double>(weights.tonicGravity) *
                    tonicProximity(j, degreesPerOctave) +
                static_cast<double>(weights.centerGravity) *
                    centerProximity(j, center);

            matrix.set(i, j, std::max(0.0, intervalWeight * gravity));
        }
    }

    matrix.normalize();
    return matrix;
}

} // namespace lumena::markov
