#pragma once

#include <cstdint>

namespace lumena::image {

/// Rec. 709 perceived luminance of an 8-bit RGB triple, returned on the same
/// 0..255 scale as its inputs (L = 0.2126*R + 0.7152*G + 0.0722*B).
///
/// Single source of truth for luma across the engine: both the brightness grid
/// (per-cell brightness -> velocity/duration/walk) and mode/scale-type
/// detection (the dark->bright valence axis) route through this so they can
/// never drift onto different coefficients. Divide the result by 255 for a
/// normalised [0, 1] value.
constexpr double luma709(double r, double g, double b) noexcept {
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

/// Convenience overload for a pixel-like value exposing `r`, `g`, `b`.
template <typename Pixel>
constexpr double luma709(const Pixel& px) noexcept {
    return luma709(static_cast<double>(px.r), static_cast<double>(px.g),
                   static_cast<double>(px.b));
}

} // namespace lumena::image
