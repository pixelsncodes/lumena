#include "scales/ScaleType.h"

namespace lumena::scales {

const std::vector<int>& intervalsFor(ScaleType type) {
    // Static per-type patterns; returned by const reference so callers can copy
    // into a Scale without reallocating on every lookup.
    static const std::vector<int> kMajorPentatonic = {0, 2, 4, 7, 9};
    static const std::vector<int> kMinorPentatonic = {0, 3, 5, 7, 10};
    static const std::vector<int> kPhrygian = {0, 1, 3, 5, 7, 8, 10};
    static const std::vector<int> kAeolian = {0, 2, 3, 5, 7, 8, 10};
    static const std::vector<int> kDorian = {0, 2, 3, 5, 7, 9, 10};
    static const std::vector<int> kMixolydian = {0, 2, 4, 5, 7, 9, 10};
    static const std::vector<int> kIonian = {0, 2, 4, 5, 7, 9, 11};
    static const std::vector<int> kLydian = {0, 2, 4, 6, 7, 9, 11};
    static const std::vector<int> kBluesMinor = {0, 3, 5, 6, 7, 10};
    static const std::vector<int> kHarmonicMinor = {0, 2, 3, 5, 7, 8, 11};

    switch (type) {
        case ScaleType::MajorPentatonic: return kMajorPentatonic;
        case ScaleType::MinorPentatonic: return kMinorPentatonic;
        case ScaleType::Phrygian:        return kPhrygian;
        case ScaleType::Aeolian:         return kAeolian;
        case ScaleType::Dorian:          return kDorian;
        case ScaleType::Mixolydian:      return kMixolydian;
        case ScaleType::Ionian:          return kIonian;
        case ScaleType::Lydian:          return kLydian;
        case ScaleType::BluesMinor:      return kBluesMinor;
        case ScaleType::HarmonicMinor:   return kHarmonicMinor;
    }
    return kMinorPentatonic;  // unreachable; keeps the compiler happy
}

const char* typeName(ScaleType type) {
    switch (type) {
        case ScaleType::MajorPentatonic: return "Major Pentatonic";
        case ScaleType::MinorPentatonic: return "Minor Pentatonic";
        case ScaleType::Phrygian:        return "Phrygian";
        case ScaleType::Aeolian:         return "Minor";
        case ScaleType::Dorian:          return "Dorian";
        case ScaleType::Mixolydian:      return "Mixolydian";
        case ScaleType::Ionian:          return "Major";
        case ScaleType::Lydian:          return "Lydian";
        case ScaleType::BluesMinor:      return "Blues";
        case ScaleType::HarmonicMinor:   return "Harmonic Minor";
    }
    return "Minor Pentatonic";
}

} // namespace lumena::scales
