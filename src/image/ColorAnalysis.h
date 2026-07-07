#pragma once

#include <cstdint>

namespace lumena::image {

class Image;

/// A colour expressed in HSV. Hue is in degrees [0, 360); saturation and value
/// are in [0, 1]. Hue is 0 for achromatic colours (grays), where it is
/// undefined.
struct Hsv {
    double h = 0.0;
    double s = 0.0;
    double v = 0.0;
};

/// The average chromatic content of an image.
struct ColorSummary {
    double hue = 0.0;         ///< Circularly averaged hue, degrees [0, 360).
    double saturation = 0.0;  ///< Mean per-pixel saturation, [0, 1].
};

/// Converts an 8-bit RGB triple to HSV.
Hsv rgbToHsv(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept;

/// Computes an image's average hue and saturation.
///
/// Hue is averaged *circularly* — each pixel's hue angle is accumulated as a
/// unit vector (sin/cos) and recovered with atan2 — so the 0°/360° wraparound
/// does not corrupt the mean (hues 350° and 10° average to 0°, not 180°).
///
/// Each pixel's contribution to the hue average is weighted by its saturation,
/// so near-gray pixels barely influence the result and a fully desaturated
/// image yields a hue of 0 with saturation ~0. Saturation itself is the plain
/// per-pixel mean. An empty image returns {0, 0}.
ColorSummary averageHueSaturation(const Image& image) noexcept;

} // namespace lumena::image
