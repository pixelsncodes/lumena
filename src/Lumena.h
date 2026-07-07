#pragma once

#include <string>

/// \file Lumena.h
/// Umbrella header for the Lumena core library.
///
/// Lumena turns an image into a MIDI melody. The pipeline is split into
/// four stages, each living in its own subdirectory under src/:
///   - image/  : overlay a grid on an image and sample per-cell brightness
///   - scales/ : manage musical scales, loaded from JSON config
///   - markov/ : drive note-to-note transitions with a Markov chain
///   - midi/   : assemble the generated notes into a MIDI sequence
///
/// This is skeleton code only: the types compile and link, but the feature
/// logic is intentionally not implemented yet.

namespace lumen {

/// Returns the library version string (semantic version, e.g. "0.1.0").
std::string version();

} // namespace lumen
