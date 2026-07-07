// Lumena test runner.
//
// A dependency-free micro test harness (see test_util.h). It exercises the
// skeleton types across the still-stubbed modules so the library links, then
// runs the behavioural suites for the implemented modules (image, scales).
// More suites are added as feature code lands.

#include <cstdio>

#include "Lumena.h"
#include "midi/MidiSequence.h"
#include "test_util.h"

// Defined in their respective test translation units.
void run_image_tests();
void run_color_analysis_tests();
void run_scale_tests();
void run_key_selector_tests();
void run_markov_tests();

namespace {

void test_version() {
    CHECK(!lumena::version().empty());
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

} // namespace

int main() {
    std::printf("Lumena test runner (library v%s)\n", lumena::version().c_str());
    std::printf("----------------------------------------\n");

    test_version();
    test_midi();
    run_image_tests();
    run_color_analysis_tests();
    run_scale_tests();
    run_key_selector_tests();
    run_markov_tests();

    const int checks = lumena::test::checkCount();
    const int failures = lumena::test::failureCount();

    std::printf("----------------------------------------\n");
    std::printf("%d checks run, %d passed, %d failed\n", checks,
                checks - failures, failures);

    return failures == 0 ? 0 : 1;
}
