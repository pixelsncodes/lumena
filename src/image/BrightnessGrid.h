#pragma once

#include <cstddef>
#include <vector>

#include "image/Image.h"

namespace lumena::image {

/// How per-cell luminance is mapped into the normalised 0.0 - 1.0 range.
enum class Normalization {
    /// Raw perceived luminance divided by 255. Preserves absolute brightness:
    /// a dim image yields uniformly low values.
    Absolute,

    /// Min-max normalised across the whole grid, so the darkest cell maps to
    /// 0.0 and the brightest to 1.0. Low-contrast images still span the full
    /// range. When every cell is identical (no contrast to stretch), all cells
    /// map to 0.0.
    Stretched,
};

/// Overlays a `columns` x `rows` grid on an Image and reduces each cell to a
/// single normalised brightness value in [0.0, 1.0].
///
/// Brightness is the average of each pixel's perceived luminance, using the
/// Rec. 709 coefficients: L = 0.2126*R + 0.7152*G + 0.0722*B.
///
/// The grid is computed once at construction; accessors are cheap reads.
class BrightnessGrid {
public:
    /// Default grid resolution (per axis) when none is specified.
    static constexpr int kDefaultResolution = 8;

    /// Maximum supported grid resolution per axis. Requested resolutions are
    /// clamped into [1, kMaxResolution].
    static constexpr int kMaxResolution = 64;

    /// Samples `image` into a `columns` x `rows` grid.
    ///
    /// `columns` and `rows` are clamped to [1, kMaxResolution]. If the image is
    /// empty, every cell is 0.0. Values are stored row-major (row 0 first,
    /// left to right).
    explicit BrightnessGrid(const Image& image,
                            int columns = kDefaultResolution,
                            int rows = kDefaultResolution,
                            Normalization mode = Normalization::Stretched);

    int columns() const noexcept { return columns_; }
    int rows() const noexcept { return rows_; }
    Normalization mode() const noexcept { return mode_; }

    /// Number of cells (`columns * rows`).
    std::size_t cellCount() const noexcept { return values_.size(); }

    /// Normalised brightness at (col, row). Returns 0.0 if either coordinate is
    /// out of range.
    float valueAt(int col, int row) const noexcept;

    /// The full grid as a flat, row-major vector of size `cellCount()`.
    const std::vector<float>& values() const noexcept { return values_; }

private:
    int columns_ = 0;
    int rows_ = 0;
    Normalization mode_ = Normalization::Stretched;
    std::vector<float> values_;
};

} // namespace lumena::image
