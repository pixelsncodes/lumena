#pragma once

#include <vector>

#include "image/ImageGrid.h"

namespace lumen::image {

/// Samples a normalised brightness value (0.0 - 1.0) for every cell of an
/// ImageGrid, in the grid's row-major cell order.
///
/// Skeleton only: returns a correctly sized buffer, but the sampling maths is
/// not implemented yet.
class BrightnessSampler {
public:
    /// Returns one brightness value per grid cell (size == grid.cellCount()).
    std::vector<float> sample(const ImageGrid& grid) const;

    // TODO: accept pixel data + weighting strategy (mean / luminance / peak).
};

} // namespace lumen::image
