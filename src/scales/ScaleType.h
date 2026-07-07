#pragma once

#include <string>
#include <vector>

namespace lumena::scales {

/// The family of scales the key selector can build. Each maps to an ascending
/// semitone interval pattern (see intervalsFor) and a display name (typeName).
///
/// The set spans, from simplest/most consonant to most coloured: the two
/// pentatonics, the seven-note diatonic modes ordered dark -> bright
/// (Phrygian, Aeolian, Dorian, Mixolydian, Ionian, Lydian), plus two
/// expressive extras (a six-note minor blues and the harmonic minor). This
/// gives the image-to-key mapping real variety instead of only major/minor
/// pentatonic.
enum class ScaleType {
    MajorPentatonic,
    MinorPentatonic,
    Phrygian,
    Aeolian,  // natural minor
    Dorian,
    Mixolydian,
    Ionian,  // major
    Lydian,
    BluesMinor,
    HarmonicMinor,
};

/// Ascending semitone offsets from the root for `type` (always starts at 0).
const std::vector<int>& intervalsFor(ScaleType type);

/// Human-readable label, e.g. "Dorian" or "Major Pentatonic".
const char* typeName(ScaleType type);

} // namespace lumena::scales
