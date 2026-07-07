// Unit tests for the colour analysis helpers (rgbToHsv, averageHueSaturation).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "image/ColorAnalysis.h"
#include "image/Image.h"
#include "test_util.h"

namespace {

using lumena::image::averageHueSaturation;
using lumena::image::ColorSummary;
using lumena::image::Hsv;
using lumena::image::Image;
using lumena::image::rgbToHsv;

Image solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return Image(w, h, std::move(px));
}

bool nearD(double a, double b, double eps) { return std::fabs(a - b) <= eps; }

// Circular closeness for hue angles (handles the 0/360 seam).
bool hueNear(double got, double want, double eps) {
    double d = std::fabs(got - want);
    d = std::min(d, 360.0 - d);
    return d <= eps;
}

void test_rgb_to_hsv_known_colors() {
    const Hsv red = rgbToHsv(255, 0, 0);
    CHECK(hueNear(red.h, 0.0, 0.01));
    CHECK(nearD(red.s, 1.0, 1e-6));
    CHECK(nearD(red.v, 1.0, 1e-6));

    CHECK(nearD(rgbToHsv(255, 255, 0).h, 60.0, 0.01));   // yellow
    CHECK(nearD(rgbToHsv(0, 255, 0).h, 120.0, 0.01));    // green
    CHECK(nearD(rgbToHsv(0, 255, 255).h, 180.0, 0.01));  // cyan
    CHECK(nearD(rgbToHsv(0, 0, 255).h, 240.0, 0.01));    // blue
    CHECK(nearD(rgbToHsv(255, 0, 255).h, 300.0, 0.01));  // magenta

    // Saturation and value.
    const Hsv green = rgbToHsv(0, 255, 0);
    CHECK(nearD(green.s, 1.0, 1e-6));
    CHECK(nearD(green.v, 1.0, 1e-6));

    // Half-bright red: hue 0, full saturation, value 0.5.
    const Hsv darkRed = rgbToHsv(128, 0, 0);
    CHECK(hueNear(darkRed.h, 0.0, 0.5));
    CHECK(nearD(darkRed.s, 1.0, 1e-6));
    CHECK(nearD(darkRed.v, 128.0 / 255.0, 1e-6));

    // Achromatic colours: saturation 0.
    CHECK(nearD(rgbToHsv(255, 255, 255).s, 0.0, 1e-9));  // white
    CHECK(nearD(rgbToHsv(255, 255, 255).v, 1.0, 1e-9));
    CHECK(nearD(rgbToHsv(0, 0, 0).s, 0.0, 1e-9));        // black
    CHECK(nearD(rgbToHsv(0, 0, 0).v, 0.0, 1e-9));
    CHECK(nearD(rgbToHsv(128, 128, 128).s, 0.0, 1e-9));  // gray
}

void test_average_solid_images() {
    const ColorSummary red = averageHueSaturation(solid(8, 8, 255, 0, 0));
    CHECK(hueNear(red.hue, 0.0, 0.5));
    CHECK(nearD(red.saturation, 1.0, 1e-6));

    const ColorSummary blue = averageHueSaturation(solid(8, 8, 0, 0, 255));
    CHECK(nearD(blue.hue, 240.0, 0.5));
    CHECK(nearD(blue.saturation, 1.0, 1e-6));

    // Grayscale: saturation ~0, hue collapses to 0.
    const ColorSummary gray = averageHueSaturation(solid(8, 8, 128, 128, 128));
    CHECK(nearD(gray.saturation, 0.0, 1e-9));
    CHECK(nearD(gray.hue, 0.0, 1e-9));

    // Empty image is safe.
    const ColorSummary empty = averageHueSaturation(Image{});
    CHECK(nearD(empty.hue, 0.0, 1e-9));
    CHECK(nearD(empty.saturation, 0.0, 1e-9));
}

void test_circular_hue_wraparound() {
    // Two saturated pixels straddling the 0/360 seam: ~10 deg and ~350 deg.
    // Confirm the source hues first.
    CHECK(nearD(rgbToHsv(255, 42, 0).h, 10.0, 2.0));
    CHECK(nearD(rgbToHsv(255, 0, 42).h, 350.0, 2.0));

    std::vector<std::uint8_t> px = {
        255, 42, 0, 255,   // hue ~10
        255, 0, 42, 255,   // hue ~350
    };
    const Image img(2, 1, std::move(px));
    const ColorSummary cs = averageHueSaturation(img);

    // Circular mean lands at ~0, NOT the naive arithmetic mean of 180.
    CHECK(hueNear(cs.hue, 0.0, 3.0));
    CHECK(!nearD(cs.hue, 180.0, 30.0));
    CHECK(nearD(cs.saturation, 1.0, 0.02));

    // A wider straddle: 60 deg and 300 deg average to 0, not 180.
    // (255,255,0) ~ 60, (255,0,255) ~ 300.
    std::vector<std::uint8_t> px2 = {
        255, 255, 0, 255,   // hue 60
        255, 0, 255, 255,   // hue 300
    };
    const Image img2(2, 1, std::move(px2));
    const ColorSummary cs2 = averageHueSaturation(img2);
    CHECK(hueNear(cs2.hue, 0.0, 1.0));
}

}  // namespace

void run_color_analysis_tests() {
    test_rgb_to_hsv_known_colors();
    test_average_solid_images();
    test_circular_hue_wraparound();
}
