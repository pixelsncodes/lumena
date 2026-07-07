#include "image/BrightnessSampler.h"

namespace lumen::image {

std::vector<float> BrightnessSampler::sample(const ImageGrid& grid) const {
    // Skeleton: allocate one slot per cell; real sampling logic comes later.
    return std::vector<float>(grid.cellCount(), 0.0f);
}

} // namespace lumen::image
