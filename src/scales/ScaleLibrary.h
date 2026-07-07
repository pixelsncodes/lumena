#pragma once

#include <optional>
#include <random>
#include <string>
#include <vector>

#include "scales/Scale.h"

namespace lumena::scales {

/// Maps a normalised brightness value to a scale-degree index.
///
/// `brightness` in [0.0, 1.0] is spread across `totalDegrees` buckets, where
/// 0.0 (darkest) yields degree 0 (lowest note) and 1.0 (brightest) yields the
/// highest degree (`totalDegrees - 1`). Out-of-range brightness is clamped, and
/// a non-positive `totalDegrees` returns 0.
int mapBrightnessToDegree(float brightness, int totalDegrees) noexcept;

/// A collection of scales loaded from JSON configuration.
///
/// Scales are stored in load order so that, given the same JSON and the same
/// seeded RNG, `randomScale` is fully reproducible. Loading is exception-free
/// across the API: malformed input yields `false` / `std::nullopt` rather than
/// throwing.
class ScaleLibrary {
public:
    /// Loads scales from a JSON file. Returns true if at least one scale was
    /// parsed; false on read/parse error or an empty result.
    bool loadFromFile(const std::string& path);

    /// Loads scales from an in-memory JSON string. Returns true if at least one
    /// scale was parsed.
    bool loadFromString(const std::string& json);

    /// Returns the scale with the given name, or std::nullopt if absent.
    std::optional<Scale> scaleByName(const std::string& name) const;

    /// Picks a scale uniformly at random using the caller-supplied RNG.
    ///
    /// The RNG is never seeded internally: seeding is the caller's job, so a
    /// fixed seed produces reproducible selections. Returns std::nullopt if the
    /// library is empty.
    std::optional<Scale> randomScale(std::mt19937& rng) const;

    /// All loaded scales, in load order.
    const std::vector<Scale>& scales() const noexcept { return scales_; }

    std::size_t size() const noexcept { return scales_.size(); }
    bool empty() const noexcept { return scales_.empty(); }

private:
    std::vector<Scale> scales_;
};

} // namespace lumena::scales
