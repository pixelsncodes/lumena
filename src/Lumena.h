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
///   - markov/ : music-theory-weighted Markov chain over scale degrees
///               (TheoryWeights, TransitionMatrix, MelodyChain)
///   - midi/   : assemble the generated notes into a tick-based sequence and
///               serialise it as a dependency-free Standard MIDI File
///
/// All four stages are implemented; the standalone `lumena_demo` executable
/// drives the whole pipeline from an image to a .mid file.

namespace lumena {

/// Returns the library version string (semantic version, e.g. "0.1.0").
std::string version();

} // namespace lumena
