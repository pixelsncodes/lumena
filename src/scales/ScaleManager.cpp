#include "scales/ScaleManager.h"

// nlohmann/json is available to this translation unit; the loaders will use it
// once implemented.
#include <nlohmann/json.hpp>

namespace lumen::scales {

bool ScaleManager::loadFromFile(const std::string& /*path*/) {
    // TODO: read file, delegate to loadFromString.
    return false;
}

bool ScaleManager::loadFromString(const std::string& /*json*/) {
    // TODO: parse the "scales" array into Scale objects with nlohmann::json.
    return false;
}

std::optional<Scale> ScaleManager::find(const std::string& name) const {
    if (auto it = scales_.find(name); it != scales_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<std::string> ScaleManager::names() const {
    std::vector<std::string> out;
    out.reserve(scales_.size());
    for (const auto& [name, scale] : scales_) {
        out.push_back(name);
    }
    return out;
}

} // namespace lumen::scales
