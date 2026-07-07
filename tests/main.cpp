// Lumena test runner.
//
// A dependency-free micro test harness (see test_util.h). It exercises the
// skeleton types across every module so that (a) the library links, and (b) the
// nlohmann/json dependency is wired up correctly, then runs the image module's
// behavioural tests. More suites are added as feature code lands.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "Lumena.h"
#include "markov/MarkovChain.h"
#include "midi/MidiSequence.h"
#include "scales/Scale.h"
#include "scales/ScaleManager.h"
#include "test_util.h"

// Defined in ImageTests.cpp.
void run_image_tests();

namespace {

void test_version() {
    CHECK(!lumena::version().empty());
}

void test_scales() {
    lumena::scales::Scale major("major", {0, 2, 4, 5, 7, 9, 11});
    CHECK(major.name() == "major");
    CHECK(major.degreeCount() == 7);

    lumena::scales::ScaleManager manager;
    CHECK(manager.empty());
    // Loaders are stubs for now; they must fail cleanly, not crash.
    CHECK(!manager.loadFromString("{}"));
    CHECK(!manager.find("major").has_value());
}

void test_markov() {
    lumena::markov::MarkovChain chain(7);
    CHECK(chain.stateCount() == 7);
}

void test_midi() {
    lumena::midi::MidiSequence seq;
    CHECK(seq.empty());
    seq.add(lumena::midi::NoteEvent{60, 100, 0.0, 1.0});
    seq.add(lumena::midi::NoteEvent{64, 90, 1.0, 0.5});
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
    std::printf("Lumena test runner (library v%s)\n", lumena::version().c_str());
    std::printf("----------------------------------------\n");

    test_version();
    test_scales();
    test_markov();
    test_midi();
    test_json_config();
    run_image_tests();

    const int checks = lumena::test::checkCount();
    const int failures = lumena::test::failureCount();

    std::printf("----------------------------------------\n");
    std::printf("%d checks run, %d passed, %d failed\n", checks,
                checks - failures, failures);

    return failures == 0 ? 0 : 1;
}
