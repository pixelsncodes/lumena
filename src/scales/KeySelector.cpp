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
constexpr int kBaseRoot = 60;            // C4; major roots span [60, 71].
constexpr int kRelativeMinorOffset = 3;  // relative minor tonic is 3 semitones down.
constexpr int kRelativeMinorSteps = 3;   // ...and 3 circle-of-fifths positions round.
constexpr double kDegreesPerPosition = 30.0;
constexpr double kGrayscaleFloor = 0.05;  // below this, hue is unreliable.
constexpr double kMajorThreshold = 0.5;

// Circle of fifths, clockwise from C. Index i also gives the pitch class
// (i * 7) % 12 above C.
const std::array<const char*, kCircleSize>& circleNames() {
    static const std::array<const char*, kCircleSize> names = {
        "C", "G", "D", "A", "E", "B", "F#", "C#/Db", "Ab", "Eb", "Bb", "F"};
    return names;
}

const std::vector<int>& majorPentatonic() {
    static const std::vector<int> pattern = {0, 2, 4, 7, 9};
    return pattern;
}

const std::vector<int>& minorPentatonic() {
    static const std::vector<int> pattern = {0, 3, 5, 7, 10};
    return pattern;
}

int pitchClass(int position) {
    return (position * 7) % 12;
}

KeyDetection buildDetection(double hue, double saturation, int position,
                            bool major) {
    KeyDetection det;
    det.hue = hue;
    det.saturation = saturation;
    det.position = position;
    det.major = major;

    const int majorRoot = kBaseRoot + pitchClass(position);
    if (major) {
        det.scale.name =
            std::string(circleNames()[position]) + " Major Pentatonic";
        det.scale.rootNote = majorRoot;
        det.scale.intervals = majorPentatonic();
    } else {
        const int minorIndex = (position + kRelativeMinorSteps) % kCircleSize;
        det.scale.name =
            std::string(circleNames()[minorIndex]) + " Minor Pentatonic";
        det.scale.rootNote = majorRoot - kRelativeMinorOffset;
        det.scale.intervals = minorPentatonic();
    }
    det.keyName = det.scale.name;
    return det;
}

}  // namespace

KeyDetection KeySelector::detect(const image::Image& image) const {
    const image::ColorSummary color = image::averageHueSaturation(image);

    // A near-gray image has no reliable hue: fall back to A minor pentatonic,
    // which is position 0 (C) resolved to its relative minor.
    if (color.saturation < kGrayscaleFloor) {
        return buildDetection(color.hue, color.saturation, /*position=*/0,
                              /*major=*/false);
    }

    int position =
        static_cast<int>(std::lround(color.hue / kDegreesPerPosition)) %
        kCircleSize;
    if (position < 0) {
        position += kCircleSize;
    }

    const bool major = color.saturation >= kMajorThreshold;
    return buildDetection(color.hue, color.saturation, position, major);
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
        return buildDetection(0.0, 0.0, /*position=*/0, /*major=*/false).scale;
    }
    return keyFromImage(image);
}

} // namespace lumena::scales
