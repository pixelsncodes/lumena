// Unit tests for the image analysis module (Image + BrightnessGrid).
//
// All fixtures are generated in code as raw RGBA buffers, so no image files are
// needed on disk.

#include <cstdint>
#include <vector>

#include "image/BrightnessGrid.h"
#include "image/Image.h"
#include "test_util.h"

namespace {

using lumena::image::BrightnessGrid;
using lumena::image::Image;
using lumena::image::Normalization;

// ---- Fixture generators ----------------------------------------------------

Image makeSolid(int width, int height, std::uint8_t r, std::uint8_t g,
                std::uint8_t b, std::uint8_t a = 255) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i + 0] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = a;
    }
    return Image(width, height, std::move(pixels));
}

// Grayscale ramp along X: leftmost column black, rightmost white.
Image makeHorizontalGradient(int width, int height) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int v = width > 1 ? (x * 255) / (width - 1) : 0;
            const auto g = static_cast<std::uint8_t>(v);
            const std::size_t i =
                (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[i + 0] = g;
            pixels[i + 1] = g;
            pixels[i + 2] = g;
            pixels[i + 3] = 255;
        }
    }
    return Image(width, height, std::move(pixels));
}

// Black/white checkerboard with square blocks of `block` pixels.
Image makeCheckerboard(int width, int height, int block) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool white = (((x / block) + (y / block)) % 2) == 0;
            const auto g = static_cast<std::uint8_t>(white ? 255 : 0);
            const std::size_t i =
                (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[i + 0] = g;
            pixels[i + 1] = g;
            pixels[i + 2] = g;
            pixels[i + 3] = 255;
        }
    }
    return Image(width, height, std::move(pixels));
}

// ---- Tests -----------------------------------------------------------------

void test_image_raw_buffer_and_access() {
    const Image img = makeSolid(3, 2, 10, 20, 30);
    CHECK(!img.empty());
    CHECK(img.width() == 3);
    CHECK(img.height() == 2);
    CHECK(img.pixelCount() == 6);

    const auto px = img.at(1, 1);
    CHECK(px.r == 10 && px.g == 20 && px.b == 30 && px.a == 255);

    // Wrong-sized buffer -> empty, not a throw.
    const Image bad(4, 4, std::vector<std::uint8_t>(10, 0));
    CHECK(bad.empty());
    CHECK(bad.width() == 0 && bad.height() == 0);

    // Non-positive dimensions -> empty.
    const Image zero(0, 5, {});
    CHECK(zero.empty());
}

void test_image_load_failures() {
    // Missing file -> nullopt (no exception across the boundary).
    CHECK(!Image::loadFromFile("/lumena/does/not/exist.png").has_value());

    // Null / empty / garbage encoded buffers -> nullopt.
    CHECK(!Image::loadFromEncoded(nullptr, 0).has_value());
    const std::vector<std::uint8_t> junk{1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(!Image::loadFromEncoded(junk.data(), junk.size()).has_value());
}

void test_luminance_coefficients() {
    // A 1x1 grid in Absolute mode returns luminance/255 directly, so we can
    // verify the Rec. 709 coefficients per channel.
    const BrightnessGrid red(makeSolid(4, 4, 255, 0, 0), 1, 1,
                             Normalization::Absolute);
    CHECK_APPROX(red.valueAt(0, 0), 0.2126f);

    const BrightnessGrid green(makeSolid(4, 4, 0, 255, 0), 1, 1,
                               Normalization::Absolute);
    CHECK_APPROX(green.valueAt(0, 0), 0.7152f);

    const BrightnessGrid blue(makeSolid(4, 4, 0, 0, 255), 1, 1,
                              Normalization::Absolute);
    CHECK_APPROX(blue.valueAt(0, 0), 0.0722f);
}

void test_all_black() {
    const Image img = makeSolid(32, 32, 0, 0, 0);

    const BrightnessGrid absolute(img, 8, 8, Normalization::Absolute);
    CHECK(absolute.cellCount() == 64);
    CHECK(absolute.columns() == 8 && absolute.rows() == 8);
    for (float v : absolute.values()) {
        CHECK_APPROX(v, 0.0f);
    }

    // Stretched on a flat image has no range to stretch -> all 0.0.
    const BrightnessGrid stretched(img, 8, 8, Normalization::Stretched);
    for (float v : stretched.values()) {
        CHECK_APPROX(v, 0.0f);
    }

    // Out-of-range accessors return 0.0.
    CHECK_APPROX(absolute.valueAt(-1, 0), 0.0f);
    CHECK_APPROX(absolute.valueAt(8, 0), 0.0f);
    CHECK_APPROX(absolute.valueAt(0, 8), 0.0f);
}

void test_all_white() {
    const Image img = makeSolid(32, 32, 255, 255, 255);

    // Absolute: white luminance == 255 -> 1.0 everywhere.
    const BrightnessGrid absolute(img, 8, 8, Normalization::Absolute);
    for (float v : absolute.values()) {
        CHECK_APPROX(v, 1.0f);
    }

    // Stretched: flat image -> no range -> all 0.0.
    const BrightnessGrid stretched(img, 8, 8, Normalization::Stretched);
    for (float v : stretched.values()) {
        CHECK_APPROX(v, 0.0f);
    }
}

void test_horizontal_gradient() {
    const Image img = makeHorizontalGradient(64, 16);

    const BrightnessGrid absolute(img, 8, 8, Normalization::Absolute);
    const BrightnessGrid stretched(img, 8, 8, Normalization::Stretched);

    // Rows are identical for a purely horizontal gradient.
    for (int col = 0; col < 8; ++col) {
        for (int row = 1; row < 8; ++row) {
            CHECK_APPROX(absolute.valueAt(col, row),
                         absolute.valueAt(col, 0));
            CHECK_APPROX(stretched.valueAt(col, row),
                         stretched.valueAt(col, 0));
        }
    }

    // Strictly increasing brightness left-to-right in both modes.
    for (int col = 1; col < 8; ++col) {
        CHECK(absolute.valueAt(col, 0) > absolute.valueAt(col - 1, 0));
        CHECK(stretched.valueAt(col, 0) > stretched.valueAt(col - 1, 0));
    }

    // Stretched spans the full range: darkest -> 0, brightest -> 1.
    CHECK_APPROX(stretched.valueAt(0, 0), 0.0f);
    CHECK_APPROX(stretched.valueAt(7, 0), 1.0f);

    // Absolute keeps the raw scale: darkest cell stays low, brightest high.
    CHECK(absolute.valueAt(0, 0) < 0.2f);
    CHECK(absolute.valueAt(7, 0) > 0.8f);

    // The two modes agree only at the extremes; stretched widens the interior,
    // so at least one interior cell must differ from its absolute counterpart.
    bool interiorDiffers = false;
    for (int col = 1; col < 7; ++col) {
        if (!lumena::test::approxEqual(stretched.valueAt(col, 0),
                                       absolute.valueAt(col, 0))) {
            interiorDiffers = true;
        }
    }
    CHECK(interiorDiffers);
}

void test_checkerboard() {
    // 16x16 image, 2px blocks -> 8x8 blocks; an 8x8 grid maps one block/cell.
    const Image img = makeCheckerboard(16, 16, 2);

    const BrightnessGrid absolute(img, 8, 8, Normalization::Absolute);
    const BrightnessGrid stretched(img, 8, 8, Normalization::Stretched);

    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const float expected = ((col + row) % 2 == 0) ? 1.0f : 0.0f;
            CHECK_APPROX(absolute.valueAt(col, row), expected);
            // Full-contrast image: stretched matches absolute here.
            CHECK_APPROX(stretched.valueAt(col, row), expected);
        }
    }
}

void test_resolution_clamping_and_defaults() {
    const Image img = makeSolid(128, 128, 128, 128, 128);

    // Default resolution is 8x8, stretched.
    const BrightnessGrid def(img);
    CHECK(def.columns() == 8 && def.rows() == 8);
    CHECK(def.mode() == Normalization::Stretched);
    CHECK(def.values().size() == def.cellCount());

    // Over-max clamps to 64; below-min clamps to 1.
    const BrightnessGrid big(img, 100, 200);
    CHECK(big.columns() == 64 && big.rows() == 64);
    CHECK(big.cellCount() == 64 * 64);

    const BrightnessGrid tiny(img, 0, -5);
    CHECK(tiny.columns() == 1 && tiny.rows() == 1);
    CHECK(tiny.cellCount() == 1);
}

void test_empty_image_grid() {
    const Image empty;  // default-constructed, 0x0
    CHECK(empty.empty());

    const BrightnessGrid grid(empty, 4, 4, Normalization::Stretched);
    CHECK(grid.cellCount() == 16);  // shape preserved
    for (float v : grid.values()) {
        CHECK_APPROX(v, 0.0f);  // no data -> all zero
    }
}

} // namespace

void run_image_tests() {
    test_image_raw_buffer_and_access();
    test_image_load_failures();
    test_luminance_coefficients();
    test_all_black();
    test_all_white();
    test_horizontal_gradient();
    test_checkerboard();
    test_resolution_clamping_and_defaults();
    test_empty_image_grid();
}
