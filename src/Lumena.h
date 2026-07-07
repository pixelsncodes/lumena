#pragma once

#include <string>

/// \file Lumena.h
/// Umbrella header for the Lumena core library.
///
/// Lumena turns an image into a MIDI melody. The pipeline is split into
/// four stages, each living in its own subdirectory under src/:
///   - image/  : load an image (Image) and reduce it to a normalised
///               brightness grid (BrightnessGrid)
///   - scales/ : manage musical scales, loaded from JSON config
///   - markov/ : drive note-to-note transitions with a Markov chain
///   - midi/   : assemble the generated notes into a MIDI sequence
///
/// The image stage is implemented; the scales, markov and midi stages are
/// still skeletons whose feature logic is not written yet.

namespace lumena {

/// Returns the library version string (semantic version, e.g. "0.1.0").
std::string version();

} // namespace lumena
