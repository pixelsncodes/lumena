#pragma once

#include <random>
#include <string>

#include "scales/Scale.h"
#include "scales/ScaleType.h"

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
    double value = 0.0;       ///< Average luminance [0, 1] (mode axis).
    int position = 0;         ///< Circle-of-fifths index [0, 11].
    ScaleType type = ScaleType::MajorPentatonic;  ///< The chosen scale family.
    bool major = true;        ///< true if `type` is a major-flavoured family.
    std::string keyName;      ///< Human-readable key, e.g. "D Dorian".
    Scale scale;              ///< The programmatically constructed scale.
};

/// Chooses a musical key from an image using the circle of fifths for the root
/// and the image's brightness/saturation for the scale family.
///
/// The image's average hue picks a position on the circle of fifths — red (0°)
/// maps to C, and every 30° advances one fifth (C, G, D, A, ...) — which is the
/// tonic pitch class. Average luminance and saturation then pick the scale
/// *type* (see chooseScaleType): washed-out images fall back to simple
/// pentatonics; vivid dark images lean into blues / harmonic minor; everything
/// else selects a diatonic mode along a dark->bright axis (Phrygian, Aeolian,
/// Dorian, Mixolydian, Ionian, Lydian). Near-grayscale images (saturation ~0)
/// have no meaningful hue and fall back to A minor pentatonic.
///
/// Scales are built from a root MIDI note plus a scale-type interval pattern,
/// so every key/type combination is available without listing them in
/// scales.json.
/// Maps perceptual brightness (`value`, Rec.601 luma in [0,1]) and `saturation`
/// ([0,1]) to a scale family. Washed-out images (low saturation) pick a
/// pentatonic; vivid + dark images pick harmonic minor or blues; otherwise a
/// diatonic mode is chosen along the dark->bright luminance axis. Pure hue is
/// not consulted here — it already sets the root.
ScaleType chooseScaleType(double saturation, double value) noexcept;

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
