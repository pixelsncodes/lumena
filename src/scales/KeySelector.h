#pragma once

#include <random>
#include <string>

#include "scales/Scale.h"

namespace lumena::image {
class Image;
}

namespace lumena::scales {

class ScaleLibrary;

/// Strategy for choosing which scale the generator plays.
enum class KeyMode {
    FromImage,  ///< Derive the key from the image's average hue and saturation.
    Random,     ///< Pick a scale at random (see ScaleLibrary::randomScale).
};

/// What KeySelector detected from an image — for UI display such as
/// "Detected: D Major Pentatonic".
struct KeyDetection {
    double hue = 0.0;         ///< Average hue in degrees [0, 360).
    double saturation = 0.0;  ///< Average saturation [0, 1].
    int position = 0;         ///< Circle-of-fifths index [0, 11].
    bool major = true;        ///< true = major pentatonic, false = relative minor.
    std::string keyName;      ///< Human-readable key, e.g. "D Major Pentatonic".
    Scale scale;              ///< The programmatically constructed scale.
};

/// Chooses a musical key from an image using the circle of fifths.
///
/// The image's average hue picks a position on the circle of fifths — red (0°)
/// maps to C, and every 30° advances one fifth (C, G, D, A, ...). Average
/// saturation picks the mode: >= 0.5 yields the major pentatonic in that key,
/// below 0.5 yields its relative minor pentatonic (three circle positions
/// round, e.g. C major -> A minor). Near-grayscale images (saturation ~0) have
/// no meaningful hue and fall back to A minor pentatonic.
///
/// Scales are built from a root MIDI note plus a pentatonic interval pattern,
/// so all 24 keys are available without listing them in scales.json.
class KeySelector {
public:
    /// Full detection result: hue, saturation, chosen key name and Scale.
    KeyDetection detect(const image::Image& image) const;

    /// The chosen Scale for an image (FromImage behaviour).
    Scale keyFromImage(const image::Image& image) const;

    /// Selects a scale according to `mode`.
    ///
    /// In Random mode, draws uniformly from `library` using `rng` (never seeded
    /// internally, for reproducibility), falling back to A minor pentatonic if
    /// the library is empty. In FromImage mode, delegates to keyFromImage.
    Scale selectScale(KeyMode mode, const image::Image& image,
                      const ScaleLibrary& library, std::mt19937& rng) const;
};

} // namespace lumena::scales
