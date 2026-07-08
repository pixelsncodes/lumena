#include "scales/KeySelector.h"

#include <array>
#include <cmath>
#include <vector>

#include "image/ColorAnalysis.h"
#include "image/Image.h"
#include "scales/ScaleLibrary.h"

namespace lumena::scales {

namespace {

constexpr int kCircleSize = 12;
constexpr int kBaseRoot = 60;            // C4; tonic roots span [60, 71].
constexpr double kDegreesPerPosition = 30.0;
constexpr double kGrayscaleFloor = 0.05;  // below this, hue is unreliable.

// Scale-type selection thresholds (see chooseScaleType).
constexpr double kWashedOutSaturation = 0.22;  // below -> pentatonic
constexpr double kVividSaturation = 0.70;       // above -> blues/harmonic minor
constexpr double kBrightPentatonic = 0.5;       // washed-out major vs minor pent
// These luma thresholds are in normalized [0,1] Rec.709-luma space (see
// luma709 / ColorAnalysis); retuning against 709 is deferred to Phase 4.
constexpr double kHarmonicMinorLuma = 0.18;     // vivid + very dark
constexpr double kBluesLuma = 0.42;             // vivid + dark

// Circle of fifths, clockwise from C. Index i also gives the pitch class
// (i * 7) % 12 above C.
const std::array<const char*, kCircleSize>& circleNames() {
    static const std::array<const char*, kCircleSize> names = {
        "C", "G", "D", "A", "E", "B", "F#", "C#/Db", "Ab", "Eb", "Bb", "F"};
    return names;
}

int pitchClass(int position) {
    return (position * 7) % 12;
}

// Major-flavoured families, for the informational KeyDetection::major flag.
bool isMajorFamily(ScaleType type) {
    switch (type) {
        case ScaleType::MajorPentatonic:
        case ScaleType::Ionian:
        case ScaleType::Lydian:
        case ScaleType::Mixolydian:
            return true;
        default:
            return false;
    }
}

KeyDetection buildDetection(double hue, double saturation, double value,
                            int position, ScaleType type) {
    KeyDetection det;
    det.hue = hue;
    det.saturation = saturation;
    det.value = value;
    det.position = position;
    det.type = type;
    det.major = isMajorFamily(type);

    det.scale.name =
        std::string(circleNames()[position]) + " " + typeName(type);
    det.scale.rootNote = kBaseRoot + pitchClass(position);
    det.scale.intervals = intervalsFor(type);
    det.keyName = det.scale.name;
    return det;
}

}  // namespace

ScaleType chooseScaleType(double saturation, double value) noexcept {
    // Washed-out images have little colour to interpret: keep it simple with a
    // pentatonic, bright -> major, dark -> minor.
    if (saturation < kWashedOutSaturation) {
        return value >= kBrightPentatonic ? ScaleType::MajorPentatonic
                                          : ScaleType::MinorPentatonic;
    }

    // Vivid *and* dark: reach for the most expressive minor colours.
    if (saturation >= kVividSaturation) {
        if (value < kHarmonicMinorLuma) return ScaleType::HarmonicMinor;
        if (value < kBluesLuma) return ScaleType::BluesMinor;
    }

    // Otherwise pick a diatonic mode along the dark -> bright luminance axis.
    static const ScaleType kModes[6] = {
        ScaleType::Phrygian,  ScaleType::Aeolian, ScaleType::Dorian,
        ScaleType::Mixolydian, ScaleType::Ionian,  ScaleType::Lydian};
    int idx = static_cast<int>(value * 6.0);
    if (idx < 0) idx = 0;
    if (idx > 5) idx = 5;
    return kModes[idx];
}

KeyDetection KeySelector::detect(const image::Image& image) const {
    const image::ColorSummary color = image::averageHueSaturation(image);

    // A near-gray image has no reliable hue: fall back to A minor pentatonic
    // (position 3 on the circle of fifths is A).
    if (color.saturation < kGrayscaleFloor) {
        return buildDetection(color.hue, color.saturation, color.value,
                              /*position=*/3, ScaleType::MinorPentatonic);
    }

    int position =
        static_cast<int>(std::lround(color.hue / kDegreesPerPosition)) %
        kCircleSize;
    if (position < 0) {
        position += kCircleSize;
    }

    const ScaleType type = chooseScaleType(color.saturation, color.value);
    return buildDetection(color.hue, color.saturation, color.value, position,
                          type);
}

Scale KeySelector::keyFromImage(const image::Image& image) const {
    return detect(image).scale;
}

Scale KeySelector::selectScale(KeyMode mode, const image::Image& image,
                               const ScaleLibrary& library,
                               std::mt19937& rng) const {
    if (mode == KeyMode::Random) {
        if (const auto scale = library.randomScale(rng)) {
            return *scale;
        }
        // Empty library: fall back to the same default as a grayscale image.
        return buildDetection(0.0, 0.0, 0.0, /*position=*/3,
                              ScaleType::MinorPentatonic)
            .scale;
    }
    return keyFromImage(image);
}

} // namespace lumena::scales
