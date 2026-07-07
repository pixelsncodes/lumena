// Lumena test runner.
//
// A dependency-free micro test harness (see test_util.h). It exercises the
// skeleton types across the still-stubbed modules so the library links, then
// runs the behavioural suites for the implemented modules (image, scales).
// More suites are added as feature code lands.

#include <cstdio>

#include "Lumena.h"
#include "test_util.h"

// Defined in their respective test translation units.
void run_image_tests();
void run_color_analysis_tests();
void run_scale_tests();
void run_key_selector_tests();
void run_markov_tests();
void run_midi_tests();

namespace {

void test_version() {
    CHECK(!lumena::version().empty());
}

} // namespace

int main() {
    std::printf("Lumena test runner (library v%s)\n", lumena::version().c_str());
    std::printf("----------------------------------------\n");

    test_version();
    run_image_tests();
    run_color_analysis_tests();
    run_scale_tests();
    run_key_selector_tests();
    run_markov_tests();
    run_midi_tests();

    const int checks = lumena::test::checkCount();
    const int failures = lumena::test::failureCount();

    std::printf("----------------------------------------\n");
    std::printf("%d checks run, %d passed, %d failed\n", checks,
                checks - failures, failures);

    return failures == 0 ? 0 : 1;
}
