// Lumena test runner.
//
// A dependency-free micro test harness: it exercises the skeleton types across
// every module so that (a) the library links, and (b) the nlohmann/json
// dependency is wired up correctly. Real behavioural tests are added as the
// feature code lands.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Lumena.h"
#include "image/BrightnessSampler.h"
#include "image/ImageGrid.h"
#include "markov/MarkovChain.h"
#include "midi/MidiSequence.h"
#include "scales/Scale.h"
#include "scales/ScaleManager.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool cond, const char* expr, const char* file, int line) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  [FAIL] %s:%d: %s\n", file, line, expr);
    }
}

#define CHECK(cond) check((cond), #cond, __FILE__, __LINE__)

void test_version() {
    CHECK(!lumen::version().empty());
}

void test_image() {
    lumen::image::ImageGrid grid(16, 12);
    CHECK(grid.columns() == 16);
    CHECK(grid.rows() == 12);
    CHECK(grid.cellCount() == 16 * 12);

    lumen::image::BrightnessSampler sampler;
    const auto brightness = sampler.sample(grid);
    CHECK(brightness.size() == grid.cellCount());
}

void test_scales() {
    lumen::scales::Scale major("major", {0, 2, 4, 5, 7, 9, 11});
    CHECK(major.name() == "major");
    CHECK(major.degreeCount() == 7);

    lumen::scales::ScaleManager manager;
    CHECK(manager.empty());
    // Loaders are stubs for now; they must fail cleanly, not crash.
    CHECK(!manager.loadFromString("{}"));
    CHECK(!manager.find("major").has_value());
}

void test_markov() {
    lumen::markov::MarkovChain chain(7);
    CHECK(chain.stateCount() == 7);
}

void test_midi() {
    lumen::midi::MidiSequence seq;
    CHECK(seq.empty());
    seq.add(lumen::midi::NoteEvent{60, 100, 0.0, 1.0});
    seq.add(lumen::midi::NoteEvent{64, 90, 1.0, 0.5});
    CHECK(seq.size() == 2);
    CHECK(seq.events().front().noteNumber == 60);
    seq.clear();
    CHECK(seq.empty());
}

// Proves the nlohmann/json dependency is fetched, linked and usable, and that
// the bundled config is valid JSON with the expected shape.
void test_json_config() {
    const std::string path = std::string(LUMENA_CONFIG_DIR) + "/scales.json";
    std::ifstream file(path);
    CHECK(file.is_open());
    if (!file.is_open()) {
        std::printf("  [INFO] could not open %s\n", path.c_str());
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    nlohmann::json config = nlohmann::json::parse(buffer.str(), nullptr, false);
    CHECK(!config.is_discarded());
    CHECK(config.contains("scales"));
    CHECK(config["scales"].is_array());
    CHECK(!config["scales"].empty());
    if (config["scales"].is_array() && !config["scales"].empty()) {
        const auto& first = config["scales"].front();
        CHECK(first.contains("name"));
        CHECK(first.contains("intervals"));
        CHECK(first["intervals"].is_array());
    }
}

} // namespace

int main() {
    std::printf("Lumena test runner (library v%s)\n", lumen::version().c_str());
    std::printf("----------------------------------------\n");

    test_version();
    test_image();
    test_scales();
    test_markov();
    test_midi();
    test_json_config();

    std::printf("----------------------------------------\n");
    std::printf("%d checks run, %d passed, %d failed\n",
                g_checks, g_checks - g_failures, g_failures);

    return g_failures == 0 ? 0 : 1;
}
