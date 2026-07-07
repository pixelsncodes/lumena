#pragma once

#include <cstddef>
#include <vector>

namespace lumena::markov {

/// First-order Markov chain over a discrete state space (e.g. scale degrees).
///
/// Holds an N x N transition matrix where transitions_[from][to] is the
/// (eventually normalised) probability of moving from state `from` to `to`.
///
/// Skeleton only: the matrix is sized, but weighting, normalisation and state
/// selection are not implemented yet.
class MarkovChain {
public:
    MarkovChain() = default;
    explicit MarkovChain(std::size_t stateCount);

    std::size_t stateCount() const noexcept { return stateCount_; }

    // TODO: setWeight(from, to, w), normalise(), next(current, rng).

private:
    std::size_t stateCount_ = 0;
    std::vector<std::vector<double>> transitions_;
};

} // namespace lumena::markov
