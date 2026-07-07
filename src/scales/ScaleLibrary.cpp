#include "scales/ScaleLibrary.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>

namespace lumena::scales {

int mapBrightnessToDegree(float brightness, int totalDegrees) noexcept {
    if (totalDegrees <= 0) {
        return 0;
    }

    // Clamp brightness into [0, 1].
    if (brightness < 0.0f) {
        brightness = 0.0f;
    } else if (brightness > 1.0f) {
        brightness = 1.0f;
    }

    int degree = static_cast<int>(brightness * static_cast<float>(totalDegrees));
    if (degree >= totalDegrees) {
        degree = totalDegrees - 1;  // brightness == 1.0 lands exactly on the top edge
    }
    return degree;
}

namespace {

using nlohmann::json;

// Extracts an integer interval array from a JSON node, ignoring non-integer
// entries. Returns false if the node is not an array.
bool readIntervals(const json& node, std::vector<int>& out) {
    if (!node.is_array()) {
        return false;
    }
    for (const auto& value : node) {
        if (value.is_number_integer()) {
            out.push_back(value.get<int>());
        }
    }
    return true;
}

} // namespace

bool ScaleLibrary::loadFromString(const std::string& jsonText) {
    const json root = json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }

    // Optional reusable interval patterns, referenced by scales via "pattern".
    std::unordered_map<std::string, std::vector<int>> patterns;
    if (root.contains("patterns") && root["patterns"].is_object()) {
        for (const auto& [key, value] : root["patterns"].items()) {
            std::vector<int> intervals;
            if (readIntervals(value, intervals) && !intervals.empty()) {
                patterns.emplace(key, std::move(intervals));
            }
        }
    }

    if (!root.contains("scales") || !root["scales"].is_array()) {
        return false;
    }

    std::vector<Scale> loaded;
    for (const auto& entry : root["scales"]) {
        if (!entry.is_object()) {
            continue;
        }

        // Name and root are required.
        if (!entry.contains("name") || !entry["name"].is_string()) {
            continue;
        }
        if (!entry.contains("root") || !entry["root"].is_number_integer()) {
            continue;
        }

        Scale scale;
        scale.name = entry["name"].get<std::string>();
        scale.rootNote = entry["root"].get<int>();

        // Intervals come from either a named pattern or an inline array.
        if (entry.contains("pattern") && entry["pattern"].is_string()) {
            const auto it = patterns.find(entry["pattern"].get<std::string>());
            if (it == patterns.end()) {
                continue;  // references an unknown pattern
            }
            scale.intervals = it->second;
        } else if (entry.contains("intervals")) {
            if (!readIntervals(entry["intervals"], scale.intervals)) {
                continue;
            }
        } else {
            continue;  // no way to determine intervals
        }

        if (scale.intervals.empty()) {
            continue;
        }
        loaded.push_back(std::move(scale));
    }

    if (loaded.empty()) {
        return false;
    }

    scales_ = std::move(loaded);
    return true;
}

bool ScaleLibrary::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadFromString(buffer.str());
}

std::optional<Scale> ScaleLibrary::scaleByName(const std::string& name) const {
    for (const auto& scale : scales_) {
        if (scale.name == name) {
            return scale;
        }
    }
    return std::nullopt;
}

std::optional<Scale> ScaleLibrary::randomScale(std::mt19937& rng) const {
    if (scales_.empty()) {
        return std::nullopt;
    }
    std::uniform_int_distribution<std::size_t> dist(0, scales_.size() - 1);
    return scales_[dist(rng)];
}

} // namespace lumena::scales
