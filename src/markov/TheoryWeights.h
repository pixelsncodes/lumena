#pragma once

#include <string>

namespace lumena::markov {

/// Tunable strengths for the music-theory rules that shape the transition
/// matrix and the dynamic (second-order) voice-leading adjustments.
///
/// Intervals are measured in scale-degree distance: a distance of 1 is a step
/// (a 2nd), 2 is a 3rd, 3 is a 4th, and so on. All strengths have musically
/// sensible defaults and can be overridden from settings.json under
/// "markov"."theory"; any missing file/key falls back to the default.
struct TheoryWeights {
    // (a) Interval penalty: geometric decay of weight per extra scale step
    //     beyond a 2nd, so stepwise motion is likeliest and leaps get rarer.
    float intervalDecay = 0.5f;

    // (d) Repetition: base weight for repeating the current degree (a distance
    //     of 0). Repeats are allowed but should not dominate steps.
    float repeatWeight = 0.5f;
    //     Multiplier applied to the repeat probability once the previous move
    //     was already a repeat, damping a third identical note. Dynamic
    //     (needs one step of history).
    float thirdRepeatDamping = 0.25f;

    // (b) Gravity: mild multiplicative pull of a destination toward the tonic
    //     degrees and toward the middle of the range, so melodies do not drift
    //     to the extremes and strand there.
    float tonicGravity = 0.4f;
    float centerGravity = 0.3f;

    // (c) Leap resolution: a leap strictly larger than this many scale steps
    //     (default 2, i.e. a 4th or wider) triggers a strong boost toward an
    //     opposite-direction step. Dynamic (needs the previous interval).
    float leapThreshold = 2.0f;
    float leapResolution = 4.0f;

    /// Loads weights from a settings JSON file, reading "markov"."theory".
    /// A missing file, parse error, or absent key falls back to the defaults
    /// above. Never throws.
    static TheoryWeights loadFromFile(const std::string& path);

    /// Loads weights from a settings JSON string. Same fallback behaviour.
    static TheoryWeights loadFromString(const std::string& json);
};

} // namespace lumena::markov
