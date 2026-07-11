// Unit tests for the circle-of-fifths key selector.

#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "image/Image.h"
#include "scales/KeySelector.h"
#include "scales/Scale.h"
#include "scales/ScaleLibrary.h"
#include "scales/ScaleType.h"
#include "test_util.h"

namespace {

using lumena::image::Image;
using lumena::scales::chooseScaleType;
using lumena::scales::intervalsFor;
using lumena::scales::KeyDetection;
using lumena::scales::KeyMode;
using lumena::scales::KeySelector;
using lumena::scales::Scale;
using lumena::scales::ScaleLibrary;
using lumena::scales::ScaleType;

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

// The root still comes from hue via the circle of fifths, independent of the
// scale type. Red (hue 0) -> position 0 -> C tonic.
void test_root_from_hue() {
    KeySelector sel;
    const KeyDetection red = sel.detect(solid(8, 8, 255, 0, 0));
    CHECK(red.position == 0);
    CHECK(red.scale.rootNote == 60);  // C4

    // Pure blue (hue 240) -> round(240/30)=8 -> position 8 -> Ab, root 68.
    const KeyDetection blue = sel.detect(solid(8, 8, 0, 0, 255));
    CHECK(blue.position == 8);
    CHECK(blue.scale.rootNote == 68);

    // Pure green (hue 120) -> position 4 -> E, root 64.
    const KeyDetection green = sel.detect(solid(8, 8, 0, 255, 0));
    CHECK(green.position == 4);
    CHECK(green.scale.rootNote == 64);
}

// chooseScaleType is the whole variety story: brightness and saturation, not
// hue, select the family.
void test_choose_scale_type() {
    // Washed-out (low saturation) -> pentatonic, bright vs dark.
    CHECK(chooseScaleType(0.10, 0.80) == ScaleType::MajorPentatonic);
    CHECK(chooseScaleType(0.10, 0.20) == ScaleType::MinorPentatonic);

    // Vivid + very dark -> harmonic minor; vivid + dark -> blues.
    CHECK(chooseScaleType(0.90, 0.10) == ScaleType::HarmonicMinor);
    CHECK(chooseScaleType(0.90, 0.35) == ScaleType::BluesMinor);

    // Mid-vivid images ride the dark->bright mode axis.
    CHECK(chooseScaleType(0.40, 0.05) == ScaleType::Phrygian);
    CHECK(chooseScaleType(0.40, 0.20) == ScaleType::Aeolian);
    CHECK(chooseScaleType(0.40, 0.45) == ScaleType::Dorian);
    CHECK(chooseScaleType(0.40, 0.60) == ScaleType::Mixolydian);
    CHECK(chooseScaleType(0.40, 0.75) == ScaleType::Ionian);
    CHECK(chooseScaleType(0.40, 0.95) == ScaleType::Lydian);
}

// Pure red is vivid and dark (luma ~0.30) -> C Blues.
void test_pure_red_is_c_blues() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 255, 0, 0));

    CHECK(d.position == 0);
    CHECK(d.saturation > 0.9);
    CHECK(d.type == ScaleType::BluesMinor);
    CHECK(!d.major);
    CHECK(d.scale.name == "C Blues");
    CHECK(d.keyName == "C Blues");
    CHECK(d.scale.rootNote == 60);
    CHECK(d.scale.intervals == intervalsFor(ScaleType::BluesMinor));
}

// Pure blue is vivid and very dark (Rec.709 luma ~0.07) -> Ab Harmonic Minor.
void test_pure_blue_is_harmonic_minor() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 0, 0, 255));

    CHECK(d.position == 8);
    CHECK(d.type == ScaleType::HarmonicMinor);
    CHECK(!d.major);
    CHECK(d.scale.name == "Ab Harmonic Minor");
    CHECK(d.scale.rootNote == 68);
}

// Pure green is vivid and bright (Rec.709 luma ~0.72) -> E Major/Ionian.
// (Under the old Rec.601 luma this was ~0.59 and landed on Mixolydian; the
// luma-unification fix moves it up one step on the dark->bright mode axis.)
void test_pure_green_is_e_major() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 0, 255, 0));

    CHECK(d.position == 4);
    CHECK(d.type == ScaleType::Ionian);
    CHECK(d.major);
    CHECK(d.scale.name == "E Major");
    CHECK(d.scale.rootNote == 64);
}

// Bright pastel red (sat ~0.30, Rec.709 luma ~0.76) -> mode axis lands on Ionian: C Major.
void test_pastel_red_is_c_major() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 255, 178, 178));

    CHECK(d.saturation > 0.22);
    CHECK(d.saturation < 0.62);  // not vivid enough for blues
    CHECK(d.position == 0);
    CHECK(d.type == ScaleType::Ionian);
    CHECK(d.major);
    CHECK(d.scale.name == "C Major");
    CHECK(d.scale.rootNote == 60);
}

// Bright pastel green (sat ~0.30, luma ~0.88) -> E Lydian.
void test_pastel_green_is_e_lydian() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 178, 255, 178));

    CHECK(d.position == 4);  // E
    CHECK(d.type == ScaleType::Lydian);
    CHECK(d.scale.name == "E Lydian");
    CHECK(d.scale.rootNote == 64);
}

// Grayscale -> defined fallback: A minor pentatonic (position 3 = A, root 69).
void test_grayscale_fallback() {
    KeySelector sel;
    const KeyDetection d = sel.detect(solid(8, 8, 128, 128, 128));

    CHECK(d.saturation < 0.05);
    CHECK(!d.major);
    CHECK(d.type == ScaleType::MinorPentatonic);
    CHECK(d.scale.name == "A Minor Pentatonic");
    CHECK(d.scale.rootNote == 69);
    CHECK(d.scale.intervals == intervalsFor(ScaleType::MinorPentatonic));

    // keyFromImage returns the same scale, and its degree math checks out.
    const Scale s = sel.keyFromImage(solid(4, 4, 200, 200, 200));
    CHECK(s.name == "A Minor Pentatonic");
    CHECK(s.noteAt(0, 2) == 69);
    CHECK(s.noteAt(5, 2) == 81);
}

// KeyMode dispatch: FromImage vs Random.
void test_select_scale_modes() {
    KeySelector sel;
    ScaleLibrary lib;
    CHECK(lib.loadFromFile(std::string(LUMENA_CONFIG_DIR) + "/scales.json"));

    const Image red = solid(8, 8, 255, 0, 0);

    // FromImage ignores the RNG/library and derives from the image.
    std::mt19937 rng(1u);
    const Scale fromImage = sel.selectScale(KeyMode::FromImage, red, lib, rng);
    CHECK(fromImage.name == "C Blues");

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
    test_root_from_hue();
    test_choose_scale_type();
    test_pure_red_is_c_blues();
    test_pure_blue_is_harmonic_minor();
    test_pure_green_is_e_major();
    test_pastel_red_is_c_major();
    test_pastel_green_is_e_lydian();
    test_grayscale_fallback();
    test_select_scale_modes();
}
