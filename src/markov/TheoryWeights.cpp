#include "markov/TheoryWeights.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace lumena::markov {

namespace {
using nlohmann::json;

// Overwrites `out` only when `obj[key]` exists and is numeric, so unknown or
// malformed keys silently keep their default.
void readFloat(const json& obj, const char* key, float& out) {
    if (obj.contains(key) && obj[key].is_number()) {
        out = obj[key].get<float>();
    }
}
}  // namespace

TheoryWeights TheoryWeights::loadFromString(const std::string& jsonText) {
    TheoryWeights weights;  // defaults

    const json root = json::parse(jsonText, nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded() || !root.is_object()) {
        return weights;
    }
    if (!root.contains("markov") || !root["markov"].is_object()) {
        return weights;
    }
    const json& markov = root["markov"];
    if (!markov.contains("theory") || !markov["theory"].is_object()) {
        return weights;
    }

    const json& theory = markov["theory"];
    readFloat(theory, "intervalDecay", weights.intervalDecay);
    readFloat(theory, "repeatWeight", weights.repeatWeight);
    readFloat(theory, "thirdRepeatDamping", weights.thirdRepeatDamping);
    readFloat(theory, "tonicGravity", weights.tonicGravity);
    readFloat(theory, "centerGravity", weights.centerGravity);
    readFloat(theory, "leapThreshold", weights.leapThreshold);
    readFloat(theory, "leapResolution", weights.leapResolution);
    return weights;
}

TheoryWeights TheoryWeights::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return TheoryWeights{};  // defaults
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return loadFromString(buffer.str());
}

} // namespace lumena::markov
