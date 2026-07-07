#pragma once

#include <string>

/// \file Lumena.h
/// Umbrella header for the Lumena core library.
///
/// Lumena turns an image into a MIDI melody. The pipeline is split into
/// four stages, each living in its own subdirectory under src/:
///   - image/  : load an image (Image), reduce it to a normalised brightness
///               grid (BrightnessGrid) and summarise its hue/saturation
///               (ColorAnalysis)
///   - scales/ : musical scales rooted at a MIDI note (Scale), loaded from
///               JSON config (ScaleLibrary) or chosen from image colour via
///               the circle of fifths (KeySelector)
///   - markov/ : drive note-to-note transitions with a Markov chain
///   - midi/   : assemble the generated notes into a MIDI sequence
///
/// The image and scales stages are implemented; the markov and midi stages are
/// still skeletons whose feature logic is not written yet.

namespace lumena {

/// Returns the library version string (semantic version, e.g. "0.1.0").
std::string version();

} // namespace lumena
