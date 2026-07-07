#include "scales/Scale.h"

namespace lumena::scales {

namespace {
constexpr int kSemitonesPerOctave = 12;
}

int Scale::usableDegrees(int octaveSpan) const noexcept {
    if (octaveSpan < 1) {
        octaveSpan = 1;
    }
    return static_cast<int>(intervals.size()) * octaveSpan;
}

int Scale::noteAt(int degree, int octaveSpan) const noexcept {
    if (intervals.empty()) {
        return rootNote;
    }
    if (octaveSpan < 1) {
        octaveSpan = 1;
    }

    const int degreesPerOctave = static_cast<int>(intervals.size());
    const int total = degreesPerOctave * octaveSpan;

    // Wrap the requested degree into [0, total), handling negatives.
    int wrapped = degree % total;
    if (wrapped < 0) {
        wrapped += total;
    }

    const int octave = wrapped / degreesPerOctave;
    const int step = wrapped % degreesPerOctave;

    return rootNote + intervals[static_cast<std::size_t>(step)] +
           kSemitonesPerOctave * octave;
}

} // namespace lumena::scales
