// Unit tests for the music-theory-weighted Markov chain
// (TheoryWeights, TransitionMatrix, MelodyChain).

#include <cmath>
#include <cstddef>
#include <random>
#include <string>
#include <vector>

#include "markov/MelodyChain.h"
#include "markov/TheoryWeights.h"
#include "markov/TransitionMatrix.h"
#include "test_util.h"

namespace {

using lumena::markov::MelodyChain;
using lumena::markov::TheoryWeights;
using lumena::markov::TransitionMatrix;

bool nearD(double a, double b, double eps) { return std::fabs(a - b) <= eps; }
bool nearF(float a, float b, float eps = 1e-6f) { return std::fabs(a - b) <= eps; }

int isign(long v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }
long ldist(std::size_t a, std::size_t b) {
    return a > b ? static_cast<long>(a - b) : static_cast<long>(b - a);
}

bool sameWeights(const TheoryWeights& a, const TheoryWeights& b) {
    return nearF(a.intervalDecay, b.intervalDecay) &&
           nearF(a.repeatWeight, b.repeatWeight) &&
           nearF(a.thirdRepeatDamping, b.thirdRepeatDamping) &&
           nearF(a.tonicGravity, b.tonicGravity) &&
           nearF(a.centerGravity, b.centerGravity) &&
           nearF(a.leapThreshold, b.leapThreshold) &&
           nearF(a.leapResolution, b.leapResolution);
}

// ---- rows sum to 1 after generation ---------------------------------------

void test_rows_normalized() {
    const TheoryWeights w;
    const TransitionMatrix m = TransitionMatrix::fromTheory(15, 5, w);
    CHECK(m.size() == 15);
    CHECK(m.isNormalized(1e-9));
    for (std::size_t i = 0; i < m.size(); ++i) {
        CHECK(nearD(m.rowSum(i), 1.0, 1e-9));
    }

    // Degenerate sizes are safe.
    const TransitionMatrix zero(0);
    CHECK(zero.isNormalized());
    const TransitionMatrix one = TransitionMatrix::fromTheory(1, 1, w);
    CHECK(one.size() == 1);
    CHECK(nearD(one.at(0, 0), 1.0, 1e-9));

    // A row summing to zero normalises to uniform.
    TransitionMatrix custom(3);
    custom.normalize();  // all-zero rows -> uniform
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(nearD(custom.rowSum(i), 1.0, 1e-9));
        CHECK(nearD(custom.at(i, 0), 1.0 / 3.0, 1e-9));
    }
}

// ---- reproducibility with a fixed seed ------------------------------------

void test_reproducible_sequence() {
    const TheoryWeights w;
    const TransitionMatrix m = TransitionMatrix::fromTheory(15, 5, w);

    std::mt19937 r1(2024u);
    std::mt19937 r2(2024u);
    MelodyChain c1(m, r1, w);
    MelodyChain c2(m, r2, w);
    std::size_t s1 = 7;
    std::size_t s2 = 7;
    for (int i = 0; i < 250; ++i) {
        s1 = c1.next(s1);
        s2 = c2.next(s2);
        CHECK(s1 == s2);
    }

    // A different seed should diverge somewhere over a long run.
    std::mt19937 r3(2024u);
    std::mt19937 r4(9999u);
    MelodyChain c3(m, r3, w);
    MelodyChain c4(m, r4, w);
    std::size_t a = 7;
    std::size_t b = 7;
    bool diverged = false;
    for (int i = 0; i < 250; ++i) {
        a = c3.next(a);
        b = c4.next(b);
        if (a != b) {
            diverged = true;
        }
    }
    CHECK(diverged);
}

// ---- steps outnumber large leaps ------------------------------------------

void test_steps_outnumber_leaps() {
    const TheoryWeights w;
    const TransitionMatrix m = TransitionMatrix::fromTheory(15, 5, w);
    std::mt19937 rng(123u);
    MelodyChain chain(m, rng, w);

    long steps = 0;
    long largeLeaps = 0;
    std::size_t cur = 7;
    for (int i = 0; i < 20000; ++i) {
        const std::size_t nxt = chain.next(cur);
        const long d = ldist(cur, nxt);
        if (d == 1) {
            ++steps;
        } else if (d >= 4) {
            ++largeLeaps;
        }
        cur = nxt;
    }
    CHECK(steps > 0);
    CHECK(steps > largeLeaps);
    CHECK(steps > 2 * largeLeaps);
}

// ---- leaps resolve opposite-direction more often than not -----------------

void test_leap_resolution() {
    const TheoryWeights w;
    const TransitionMatrix m = TransitionMatrix::fromTheory(21, 7, w);
    std::mt19937 rng(777u);
    MelodyChain chain(m, rng, w);

    long resolveOpposite = 0;
    long continueSame = 0;
    int pendingLeapSign = 0;  // sign of the leap just taken, else 0
    std::size_t cur = 10;

    for (int i = 0; i < 60000; ++i) {
        const std::size_t nxt = chain.next(cur);
        const int interval = static_cast<int>(nxt) - static_cast<int>(cur);

        if (pendingLeapSign != 0) {
            const int s = isign(interval);
            if (s == -pendingLeapSign) {
                ++resolveOpposite;
            } else if (s == pendingLeapSign) {
                ++continueSame;
            }
            // s == 0 (a repeat) counts as neither.
        }

        pendingLeapSign = (std::abs(interval) > static_cast<int>(w.leapThreshold))
                              ? isign(interval)
                              : 0;
        cur = nxt;
    }

    CHECK(resolveOpposite > 0);
    CHECK(continueSame > 0);
    CHECK(resolveOpposite > continueSame);
}

// ---- nextBiased behaviour --------------------------------------------------

void test_next_biased_target() {
    const TheoryWeights w;
    const TransitionMatrix m = TransitionMatrix::fromTheory(15, 5, w);
    std::mt19937 rng(5u);
    MelodyChain chain(m, rng, w);

    // bias 1.0 always lands exactly on the (rounded, clamped) target.
    const std::size_t currents[] = {0, 3, 7, 14};
    const int targets[] = {0, 1, 7, 13, 14};
    for (std::size_t cur : currents) {
        for (int t : targets) {
            chain.reset();
            const std::size_t got =
                chain.nextBiased(cur, static_cast<float>(t), 1.0f);
            CHECK(got == static_cast<std::size_t>(t));
        }
    }

    // Rounding and clamping of the target.
    chain.reset();
    CHECK(chain.nextBiased(7, 3.4f, 1.0f) == 3);
    chain.reset();
    CHECK(chain.nextBiased(7, 3.6f, 1.0f) == 4);
    chain.reset();
    CHECK(chain.nextBiased(7, -5.0f, 1.0f) == 0);
    chain.reset();
    CHECK(chain.nextBiased(7, 999.0f, 1.0f) == 14);
}

void test_next_biased_zero_is_markov() {
    const TheoryWeights w;
    const TransitionMatrix m = TransitionMatrix::fromTheory(15, 5, w);

    // bias 0.0 reproduces next() exactly (same RNG usage), given the same
    // seed and cleared history each step -> pure first-order Markov.
    std::mt19937 ra(4242u);
    std::mt19937 rb(4242u);
    MelodyChain ca(m, ra, w);
    MelodyChain cb(m, rb, w);
    const std::size_t cur = 6;
    for (int i = 0; i < 4000; ++i) {
        ca.reset();
        cb.reset();
        const std::size_t a = ca.next(cur);
        const std::size_t b = cb.nextBiased(cur, 12.0f, 0.0f);
        CHECK(a == b);
    }

    // And the empirical distribution matches the transition matrix row.
    std::mt19937 rc(2020u);
    MelodyChain cc(m, rc, w);
    const int trials = 60000;
    std::vector<long> hist(m.size(), 0);
    for (int i = 0; i < trials; ++i) {
        cc.reset();
        const std::size_t x = cc.nextBiased(6, 0.0f, 0.0f);
        ++hist[x];
    }
    for (std::size_t j = 0; j < m.size(); ++j) {
        const double empirical = static_cast<double>(hist[j]) / trials;
        CHECK(nearD(empirical, m.at(6, j), 0.02));
    }
}

// ---- settings.json fallback ------------------------------------------------

void test_settings_fallback() {
    const TheoryWeights def;

    // Missing file -> defaults.
    CHECK(sameWeights(TheoryWeights::loadFromFile("/lumena/no/settings.json"),
                      def));

    // Malformed / empty / theory-less JSON -> defaults.
    CHECK(sameWeights(TheoryWeights::loadFromString("{ not valid json"), def));
    CHECK(sameWeights(TheoryWeights::loadFromString("{}"), def));
    CHECK(sameWeights(
        TheoryWeights::loadFromString(R"({"markov":{"octaveSpan":2}})"), def));

    // Non-numeric value for a key -> that key keeps its default.
    const TheoryWeights bogus = TheoryWeights::loadFromString(
        R"({"markov":{"theory":{"intervalDecay":"oops"}}})");
    CHECK(nearF(bogus.intervalDecay, def.intervalDecay));

    // Partial override -> named keys change, the rest stay default.
    const TheoryWeights partial = TheoryWeights::loadFromString(
        R"({"markov":{"theory":{"intervalDecay":0.9,"leapResolution":7.0}}})");
    CHECK(nearF(partial.intervalDecay, 0.9f));
    CHECK(nearF(partial.leapResolution, 7.0f));
    CHECK(nearF(partial.repeatWeight, def.repeatWeight));
    CHECK(nearF(partial.tonicGravity, def.tonicGravity));

    // The bundled settings.json loads and provides every key.
    const TheoryWeights fromFile = TheoryWeights::loadFromFile(
        std::string(LUMENA_CONFIG_DIR) + "/settings.json");
    CHECK(nearF(fromFile.intervalDecay, 0.5f));
    CHECK(nearF(fromFile.repeatWeight, 0.5f));
    CHECK(nearF(fromFile.leapResolution, 4.0f));

    // Building a matrix from file-loaded weights still yields valid rows.
    const TransitionMatrix m = TransitionMatrix::fromTheory(10, 5, fromFile);
    CHECK(m.isNormalized(1e-9));
}

}  // namespace

void run_markov_tests() {
    test_rows_normalized();
    test_reproducible_sequence();
    test_steps_outnumber_leaps();
    test_leap_resolution();
    test_next_biased_target();
    test_next_biased_zero_is_markov();
    test_settings_fallback();
}
