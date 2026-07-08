#include "image/BrightnessGrid.h"

#include <algorithm>

#include "image/Luma.h"

namespace lumena::image {

namespace {

int clampResolution(int requested) {
    if (requested < 1) {
        return 1;
    }
    if (requested > BrightnessGrid::kMaxResolution) {
        return BrightnessGrid::kMaxResolution;
    }
    return requested;
}

// Rec. 709 perceived luminance for an 8-bit pixel, in the 0..255 range.
double perceivedLuminance(const Rgba& px) {
    return luma709(px);
}

// Maps cell index `i` of `divisions` onto the pixel span [begin, end) of an
// axis of length `extent`, so cells partition the axis with no gaps/overlap.
void cellSpan(int i, int divisions, int extent, int& begin, int& end) {
    begin = static_cast<int>(static_cast<long long>(i) * extent / divisions);
    end = static_cast<int>(static_cast<long long>(i + 1) * extent / divisions);
    if (end <= begin && begin < extent) {
        end = begin + 1;  // guarantee at least one pixel when the axis allows it
    }
}

} // namespace

BrightnessGrid::BrightnessGrid(const Image& image, int columns, int rows,
                               Normalization mode)
    : columns_(clampResolution(columns)),
      rows_(clampResolution(rows)),
      mode_(mode),
      values_(static_cast<std::size_t>(clampResolution(columns)) *
                  static_cast<std::size_t>(clampResolution(rows)),
              0.0f) {
    if (image.empty()) {
        return;  // all cells already 0.0
    }

    const int width = image.width();
    const int height = image.height();

    // First pass: average perceived luminance (0..255) per cell.
    std::vector<double> cellLuminance(values_.size(), 0.0);

    for (int row = 0; row < rows_; ++row) {
        int y0 = 0;
        int y1 = 0;
        cellSpan(row, rows_, height, y0, y1);

        for (int col = 0; col < columns_; ++col) {
            int x0 = 0;
            int x1 = 0;
            cellSpan(col, columns_, width, x0, x1);

            double sum = 0.0;
            std::size_t count = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    sum += perceivedLuminance(image.at(x, y));
                    ++count;
                }
            }

            const std::size_t index =
                static_cast<std::size_t>(row) *
                    static_cast<std::size_t>(columns_) +
                static_cast<std::size_t>(col);
            cellLuminance[index] = count > 0 ? sum / static_cast<double>(count)
                                             : 0.0;
        }
    }

    // Second pass: normalise into [0, 1] according to the selected mode.
    if (mode_ == Normalization::Absolute) {
        for (std::size_t i = 0; i < values_.size(); ++i) {
            values_[i] = static_cast<float>(cellLuminance[i] / 255.0);
        }
        return;
    }

    // Stretched (min-max across the grid).
    const auto [minIt, maxIt] =
        std::minmax_element(cellLuminance.begin(), cellLuminance.end());
    const double minLum = *minIt;
    const double maxLum = *maxIt;
    const double range = maxLum - minLum;

    if (range <= 0.0) {
        return;  // no contrast to stretch; leave all cells at 0.0
    }

    for (std::size_t i = 0; i < values_.size(); ++i) {
        values_[i] = static_cast<float>((cellLuminance[i] - minLum) / range);
    }
}

float BrightnessGrid::valueAt(int col, int row) const noexcept {
    if (col < 0 || col >= columns_ || row < 0 || row >= rows_) {
        return 0.0f;
    }
    const std::size_t index = static_cast<std::size_t>(row) *
                                  static_cast<std::size_t>(columns_) +
                              static_cast<std::size_t>(col);
    return values_[index];
}

} // namespace lumena::image
