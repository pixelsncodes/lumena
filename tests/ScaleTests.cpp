// Unit tests for the scale system (Scale + ScaleLibrary).

#include <random>
#include <string>
#include <vector>

#include "scales/ScaleLibrary.h"
#include "scales/Scale.h"
#include "test_util.h"

namespace {

using lumena::scales::Scale;
using lumena::scales::ScaleLibrary;
using lumena::scales::mapBrightnessToDegree;

Scale aMinorPentatonic() {
    return Scale{"A Minor Pentatonic", 57, {0, 3, 5, 7, 10}};
}

// ---- Interval math & octave wrapping --------------------------------------

void test_note_at_and_wrapping() {
    const Scale a = aMinorPentatonic();

    CHECK(a.degreesPerOctave() == 5);
    CHECK(a.usableDegrees(2) == 10);
    CHECK(a.usableDegrees(0) == 5);  // span < 1 treated as 1

    // Within the first octave.
    CHECK(a.noteAt(0, 2) == 57);  // A3 (root)
    CHECK(a.noteAt(1, 2) == 60);  // +3
    CHECK(a.noteAt(2, 2) == 62);  // +5
    CHECK(a.noteAt(3, 2) == 64);  // +7
    CHECK(a.noteAt(4, 2) == 67);  // +10

    // Second octave: degree 5 rolls up a full octave.
    CHECK(a.noteAt(5, 2) == 69);  // root + 12 == A4
    CHECK(a.noteAt(9, 2) == 79);  // +10 + 12

    // Degrees outside [0, usableDegrees) wrap around the span.
    CHECK(a.noteAt(10, 2) == a.noteAt(0, 2));   // wraps to degree 0
    CHECK(a.noteAt(-1, 2) == a.noteAt(9, 2));    // wraps to top degree
    CHECK(a.noteAt(11, 2) == a.noteAt(1, 2));

    // A wider span exposes more octaves without wrapping.
    CHECK(a.noteAt(10, 3) == 81);  // root + 24
    CHECK(a.usableDegrees(3) == 15);

    // Empty scale is safe: returns the root.
    const Scale empty{"Empty", 60, {}};
    CHECK(empty.noteAt(3, 2) == 60);
    CHECK(empty.degreesPerOctave() == 0);
}

// ---- JSON loading of all configured scales --------------------------------

void test_json_loading() {
    ScaleLibrary lib;
    const std::string path = std::string(LUMENA_CONFIG_DIR) + "/scales.json";
    CHECK(lib.loadFromFile(path));
    CHECK(lib.size() == 6);

    struct Expected {
        const char* name;
        int root;
    };
    const std::vector<Expected> expected = {
        {"A Minor Pentatonic", 57},  {"F# Minor Pentatonic", 54},
        {"C Major Pentatonic", 60},  {"E Minor Pentatonic", 52},
        {"G Major Pentatonic", 55},  {"D Major Pentatonic", 62},
    };

    for (const auto& e : expected) {
        const auto scale = lib.scaleByName(e.name);
        CHECK(scale.has_value());
        if (scale) {
            CHECK(scale->rootNote == e.root);
            CHECK(scale->intervals.size() == 5);  // all pentatonic
        }
    }

    // Interval math survives the round-trip through JSON.
    const auto loadedA = lib.scaleByName("A Minor Pentatonic");
    CHECK(loadedA.has_value());
    if (loadedA) {
        CHECK(loadedA->noteAt(0, 2) == 57);
        CHECK(loadedA->noteAt(5, 2) == 69);
    }

    // Pattern resolution: a major-pentatonic scale has the right intervals.
    const auto loadedC = lib.scaleByName("C Major Pentatonic");
    CHECK(loadedC.has_value());
    if (loadedC) {
        const std::vector<int> majorPent = {0, 2, 4, 7, 9};
        CHECK(loadedC->intervals == majorPent);
    }
}

void test_load_failures() {
    ScaleLibrary lib;
    CHECK(!lib.loadFromFile("/lumena/no/such/scales.json"));
    CHECK(!lib.loadFromString("this is not json"));
    CHECK(!lib.loadFromString("{}"));                 // no scales array
    CHECK(!lib.loadFromString(R"({"scales": []})"));  // empty -> false
    CHECK(lib.empty());

    // Inline intervals (no pattern table) are also accepted.
    CHECK(lib.loadFromString(
        R"({"scales":[{"name":"Custom","root":60,"intervals":[0,4,7]}]})"));
    CHECK(lib.size() == 1);
    const auto custom = lib.scaleByName("Custom");
    CHECK(custom.has_value());
    if (custom) {
        CHECK(custom->rootNote == 60);
        CHECK(custom->noteAt(1, 1) == 64);
    }

    // A scale referencing an unknown pattern is skipped, leaving none loaded.
    CHECK(!lib.loadFromString(
        R"({"scales":[{"name":"Bad","root":60,"pattern":"missing"}]})"));
}

// ---- Name lookup ----------------------------------------------------------

void test_name_lookup() {
    ScaleLibrary lib;
    CHECK(lib.loadFromFile(std::string(LUMENA_CONFIG_DIR) + "/scales.json"));

    CHECK(lib.scaleByName("G Major Pentatonic").has_value());
    CHECK(!lib.scaleByName("Z Diminished Bebop").has_value());
    CHECK(!lib.scaleByName("").has_value());
}

// ---- Brightness -> degree mapping -----------------------------------------

void test_brightness_mapping() {
    CHECK(mapBrightnessToDegree(0.0f, 10) == 0);   // darkest -> lowest
    CHECK(mapBrightnessToDegree(0.5f, 10) == 5);   // middle
    CHECK(mapBrightnessToDegree(1.0f, 10) == 9);   // brightest -> highest

    // Edge clamping.
    CHECK(mapBrightnessToDegree(-0.25f, 10) == 0);
    CHECK(mapBrightnessToDegree(1.25f, 10) == 9);

    // Degenerate degree counts.
    CHECK(mapBrightnessToDegree(0.7f, 1) == 0);
    CHECK(mapBrightnessToDegree(0.7f, 0) == 0);
    CHECK(mapBrightnessToDegree(0.7f, -3) == 0);

    // Always in range for a spread of values.
    const int total = 5;
    for (int i = 0; i <= 20; ++i) {
        const int d = mapBrightnessToDegree(static_cast<float>(i) / 20.0f, total);
        CHECK(d >= 0 && d < total);
    }
}

// ---- Seeded RNG reproducibility -------------------------------------------

void test_random_scale_reproducible() {
    ScaleLibrary lib;
    CHECK(lib.loadFromFile(std::string(LUMENA_CONFIG_DIR) + "/scales.json"));

    // Two generators seeded identically must yield identical selections.
    std::mt19937 rngA(12345u);
    std::mt19937 rngB(12345u);
    for (int i = 0; i < 25; ++i) {
        const auto a = lib.randomScale(rngA);
        const auto b = lib.randomScale(rngB);
        CHECK(a.has_value() && b.has_value());
        if (a && b) {
            CHECK(a->name == b->name);
        }
    }

    // A fresh generator with the same seed reproduces the very first pick.
    std::mt19937 rngC(12345u);
    const auto first = lib.randomScale(rngC);
    std::mt19937 rngD(12345u);
    const auto firstAgain = lib.randomScale(rngD);
    CHECK(first.has_value() && firstAgain.has_value());
    if (first && firstAgain) {
        CHECK(first->name == firstAgain->name);
    }

    // Empty library yields nullopt rather than throwing.
    ScaleLibrary emptyLib;
    std::mt19937 rng(1u);
    CHECK(!emptyLib.randomScale(rng).has_value());
}

} // namespace

void run_scale_tests() {
    test_note_at_and_wrapping();
    test_json_loading();
    test_load_failures();
    test_name_lookup();
    test_brightness_mapping();
    test_random_scale_reproducible();
}
