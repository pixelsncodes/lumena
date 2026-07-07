#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "scales/Scale.h"

namespace lumena::scales {

/// Owns the set of scales available to the generator and loads them from JSON
/// config (see config/scales.json). Parsing is backed by nlohmann/json.
///
/// Skeleton only: the public surface is in place, but the loaders currently
/// do nothing and report failure.
class ScaleManager {
public:
    /// Loads scales from a JSON file on disk. Returns true on success.
    bool loadFromFile(const std::string& path);

    /// Loads scales from an in-memory JSON string. Returns true on success.
    bool loadFromString(const std::string& json);

    /// Looks up a scale by name; std::nullopt if not present.
    std::optional<Scale> find(const std::string& name) const;

    /// Names of all loaded scales.
    std::vector<std::string> names() const;

    std::size_t size() const noexcept { return scales_.size(); }
    bool empty() const noexcept { return scales_.empty(); }

private:
    std::unordered_map<std::string, Scale> scales_;
};

} // namespace lumena::scales
