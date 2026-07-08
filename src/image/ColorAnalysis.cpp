#include "image/ColorAnalysis.h"

#include <algorithm>
#include <cmath>

#include "image/Image.h"
#include "image/Luma.h"

namespace lumena::image {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDegPerRad = 180.0 / kPi;
constexpr double kRadPerDeg = kPi / 180.0;
}  // namespace

Hsv rgbToHsv(std::uint8_t r8, std::uint8_t g8, std::uint8_t b8) noexcept {
    const double r = r8 / 255.0;
    const double g = g8 / 255.0;
    const double b = b8 / 255.0;

    const double maxC = std::max({r, g, b});
    const double minC = std::min({r, g, b});
    const double delta = maxC - minC;

    Hsv hsv;
    hsv.v = maxC;
    hsv.s = (maxC <= 0.0) ? 0.0 : (delta / maxC);

    if (delta <= 0.0) {
        hsv.h = 0.0;  // achromatic: hue undefined, report 0
        return hsv;
    }

    double hue = 0.0;
    if (maxC == r) {
        hue = 60.0 * std::fmod((g - b) / delta, 6.0);
    } else if (maxC == g) {
        hue = 60.0 * (((b - r) / delta) + 2.0);
    } else {
        hue = 60.0 * (((r - g) / delta) + 4.0);
    }
    if (hue < 0.0) {
        hue += 360.0;
    }
    hsv.h = hue;
    return hsv;
}

ColorSummary averageHueSaturation(const Image& image) noexcept {
    if (image.empty()) {
        return {};
    }

    double sumSin = 0.0;
    double sumCos = 0.0;
    double sumSaturation = 0.0;
    double sumLuma = 0.0;

    const int width = image.width();
    const int height = image.height();
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Rgba px = image.at(x, y);
            const Hsv hsv = rgbToHsv(px.r, px.g, px.b);

            const double radians = hsv.h * kRadPerDeg;
            // Weight the hue direction by saturation so grays barely count.
            sumSin += hsv.s * std::sin(radians);
            sumCos += hsv.s * std::cos(radians);
            sumSaturation += hsv.s;

            // Perceptual brightness (Rec.709 luma) on [0, 1]. Used as the
            // "valence" axis for scale-mode selection: dark images lean toward
            // darker modes, bright ones toward brighter modes. Shares the single
            // luma709 source with the brightness grid so the two never diverge.
            sumLuma += luma709(px) / 255.0;
        }
    }

    const double count = static_cast<double>(width) * static_cast<double>(height);

    ColorSummary summary;
    double hue = std::atan2(sumSin, sumCos) * kDegPerRad;
    if (hue < 0.0) {
        hue += 360.0;
    }
    summary.hue = hue;
    summary.saturation = sumSaturation / count;
    summary.value = sumLuma / count;
    return summary;
}

} // namespace lumena::image
