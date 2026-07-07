#pragma once

#include <cstddef>

namespace lumen::image {

/// A rectangular grid overlaid on a source image.
///
/// Each cell aggregates the pixels beneath it so downstream stages can sample
/// a single brightness value per cell. Cells are addressed by (column, row)
/// and iterated in row-major order.
///
/// Skeleton only: dimensions are tracked, but no pixel data is stored or
/// mapped yet.
class ImageGrid {
public:
    ImageGrid() = default;
    ImageGrid(std::size_t columns, std::size_t rows);

    std::size_t columns() const noexcept { return columns_; }
    std::size_t rows() const noexcept { return rows_; }
    std::size_t cellCount() const noexcept { return columns_ * rows_; }

    // TODO: attach pixel data, map (column,row) -> pixel region, and expose
    //       per-cell pixel access for the brightness sampler.

private:
    std::size_t columns_ = 0;
    std::size_t rows_ = 0;
};

} // namespace lumen::image
