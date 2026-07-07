#include "markov/MarkovChain.h"

namespace lumen::markov {

MarkovChain::MarkovChain(std::size_t stateCount)
    : stateCount_(stateCount),
      transitions_(stateCount, std::vector<double>(stateCount, 0.0)) {}

} // namespace lumen::markov
