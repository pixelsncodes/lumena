#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace lumena::scales {

/// A musical scale rooted at an absolute MIDI note.
///
/// The scale is described by a set of ascending semitone offsets (`intervals`)
/// from `rootNote`, conventionally starting at 0. For example A minor
/// pentatonic is rootNote = 57 (A3) with intervals { 0, 3, 5, 7, 10 }.
///
/// A plain aggregate: construct it with brace-init and read the public members
/// directly. `noteAt` provides the degree -> MIDI-note mapping.
struct Scale {
    std::string name;            ///< Human-readable label, e.g. "A Minor Pentatonic".
    int rootNote = 0;            ///< MIDI note of degree 0 (e.g. 57 for A3).
    std::vector<int> intervals;  ///< Semitone offsets from the root, ascending.

    /// Number of distinct scale degrees within a single octave.
    std::size_t degreesPerOctave() const noexcept { return intervals.size(); }

    /// Total number of usable degrees across `octaveSpan` octaves. An
    /// octaveSpan below 1 is treated as 1.
    int usableDegrees(int octaveSpan) const noexcept;

    /// Maps a scale-degree index to an absolute MIDI note, wrapping across
    /// `octaveSpan` octaves.
    ///
    /// Degrees advance through the interval pattern and roll up an octave every
    /// `degreesPerOctave()` steps: for A minor pentatonic (5 degrees), degree 0
    /// is the root, degree 5 is root + 12. Indices outside
    /// [0, usableDegrees(octaveSpan)) wrap around that range, so the returned
    /// note always lies within the configured span. Returns `rootNote` if the
    /// scale has no intervals.
    int noteAt(int degree, int octaveSpan) const noexcept;
};

} // namespace lumena::scales
