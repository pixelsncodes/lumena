// Unit tests for the circle-of-fifths key selector.

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "image/Image.h"
#include "scales/KeySelector.h"
#include "scales/Scale.h"
#include "scales/ScaleLibrary.h"
#include "test_util.h"

namespace {

using lumena::image::Image;
using lumena::scales::KeyDetection;
using lumena::scales::KeyMode;
using lumena::scales::KeySelector;
using lumena::scales::Scale;
using lumena::scales::ScaleLibrary;

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

const std::vector<int> kMajorPent = {0, 2, 4, 7, 9};
const std::vector<int> kMinorPent = {0, 3, 5, 7, 10};

// Pure red (hue 0) -> circle position 0 -> C major pentatonic.
void test_pure_red_is_c_major() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 255, 0, 0));

    CHECK(d.position == 0);
    CHECK(d.major);
    CHECK(d.saturation > 0.9);
    CHECK(d.scale.name == "C Major Pentatonic");
    CHECK(d.keyName == "C Major Pentatonic");
    CHECK(d.scale.rootNote == 60);  // C4
    CHECK(d.scale.intervals == kMajorPent);
}

// Pure blue (hue 240) -> round(240/30)=8 -> position 8 -> Ab.
void test_pure_blue_position() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 0, 0, 255));

    CHECK(d.position == 8);
    CHECK(d.major);
    CHECK(d.scale.name == "Ab Major Pentatonic");
    CHECK(d.scale.rootNote == 68);  // pitch class (8*7)%12 = 8 -> 60+8
    CHECK(d.scale.intervals == kMajorPent);
}

// Pure green (hue 120) -> position 4 -> E major pentatonic.
void test_pure_green_is_e_major() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 0, 255, 0));

    CHECK(d.position == 4);
    CHECK(d.major);
    CHECK(d.scale.name == "E Major Pentatonic");
    CHECK(d.scale.rootNote == 64);  // pitch class (4*7)%12 = 4 -> 60+4
}

// Low-saturation red (hue 0, sat ~0.30) -> relative minor of C -> A minor.
void test_low_saturation_relative_minor() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 255, 178, 178));

    CHECK(d.saturation > 0.05);  // not grayscale
    CHECK(d.saturation < 0.5);   // triggers minor
    CHECK(d.position == 0);
    CHECK(!d.major);
    CHECK(d.scale.name == "A Minor Pentatonic");
    CHECK(d.scale.rootNote == 57);  // A3 = C4 - 3
    CHECK(d.scale.intervals == kMinorPent);
}

// Low-saturation green (hue 120, sat ~0.30) -> relative minor of E -> C# minor.
void test_low_saturation_non_c_position() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 178, 255, 178));

    CHECK(d.saturation > 0.05);
    CHECK(d.saturation < 0.5);
    CHECK(d.position == 4);  // E
    CHECK(!d.major);
    CHECK(d.scale.name == "C#/Db Minor Pentatonic");  // relative minor of E
    CHECK(d.scale.rootNote == 61);  // E root 64 - 3
}

// Grayscale -> defined fallback: A minor pentatonic.
void test_grayscale_fallback() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 128, 128, 128));

    CHECK(d.saturation < 0.05);
    CHECK(!d.major);
    CHECK(d.scale.name == "A Minor Pentatonic");
    CHECK(d.scale.rootNote == 57);
    CHECK(d.scale.intervals == kMinorPent);

    // keyFromImage returns the same scale, and its degree math checks out.
    const Scale s = sel.keyFromImage(solid(4, 4, 200, 200, 200));
    CHECK(s.name == "A Minor Pentatonic");
    CHECK(s.noteAt(0, 2) == 57);
    CHECK(s.noteAt(5, 2) == 69);
}

// KeyMode dispatch: FromImage vs Random.
void test_select_scale_modes() {
    KeySelector sel;
    ScaleLibrary lib;
    CHECK(lib.loadFromFile(std::string(LUMENA_CONFIG_DIR) + "/scales.json"));

    const Image red = solid(8, 8, 255, 0, 0);

    // FromImage ignores the RNG/library and derives from hue.
    std::mt19937 rng(1u);
    const Scale fromImage = sel.selectScale(KeyMode::FromImage, red, lib, rng);
    CHECK(fromImage.name == "C Major Pentatonic");

    // Random draws from the library, reproducibly, matching ScaleLibrary.
    std::mt19937 seededA(999u);
    std::mt19937 seededB(999u);
    const Scale randomA = sel.selectScale(KeyMode::Random, red, lib, seededA);
    const auto randomB = lib.randomScale(seededB);
    CHECK(randomB.has_value());
    if (randomB) {
        CHECK(randomA.name == randomB->name);
    }

    // Random with an empty library falls back to A minor pentatonic.
    ScaleLibrary emptyLib;
    std::mt19937 rng2(7u);
    const Scale fallback =
        sel.selectScale(KeyMode::Random, red, emptyLib, rng2);
    CHECK(fallback.name == "A Minor Pentatonic");
}

}  // namespace

void run_key_selector_tests() {
    test_pure_red_is_c_major();
    test_pure_blue_position();
    test_pure_green_is_e_major();
    test_low_saturation_relative_minor();
    test_low_saturation_non_c_position();
    test_grayscale_fallback();
    test_select_scale_modes();
}
