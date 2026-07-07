#include "midi/MidiSequence.h"

#include <algorithm>
#include <cmath>

namespace lumena::midi {

namespace {

int clampMidiValue(int value) noexcept {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return value;
}

/// Rounds beats to an absolute tick, clamped at zero (no negative time).
std::uint32_t beatsToTick(double beats, int ppq) noexcept {
    if (beats <= 0.0) return 0;
    const double ticks = std::llround(beats * static_cast<double>(ppq));
    return static_cast<std::uint32_t>(ticks < 0.0 ? 0.0 : ticks);
}

} // namespace

MidiSequence::MidiSequence(const std::vector<Note>& notes, double tempoBpm,
                           int ppq)
    : tempoBpm_(tempoBpm > 0.0 ? tempoBpm : kDefaultTempoBpm),
      ppq_(ppq > 0 ? ppq : kDefaultPpq) {
    events_.reserve(notes.size() * 2);

    for (const Note& note : notes) {
        if (note.lengthBeats <= 0.0) {
            continue;  // Zero/negative-length notes make no sound; drop them.
        }

        const std::uint32_t onTick = beatsToTick(note.startBeats, ppq_);
        std::uint32_t offTick =
            beatsToTick(note.startBeats + note.lengthBeats, ppq_);
        if (offTick <= onTick) {
            offTick = onTick + 1;  // Guarantee an audible, non-zero duration.
        }

        const int pitch = clampMidiValue(note.noteNumber);
        events_.push_back({onTick, MidiEventType::NoteOn, pitch,
                           clampMidiValue(note.velocity)});
        events_.push_back({offTick, MidiEventType::NoteOff, pitch, 0});
    }

    // Sort by tick, then by type so that note-offs precede note-ons sharing a
    // tick. stable_sort keeps notes added earlier ahead of later ones at an
    // otherwise-equal position, making the output deterministic.
    std::stable_sort(events_.begin(), events_.end(),
                     [](const TimedEvent& a, const TimedEvent& b) {
                         if (a.tick != b.tick) return a.tick < b.tick;
                         return static_cast<int>(a.type) <
                                static_cast<int>(b.type);
                     });
}

std::uint32_t MidiSequence::microsecondsPerQuarter() const noexcept {
    // 60,000,000 microseconds per minute / beats per minute.
    return static_cast<std::uint32_t>(
        std::llround(60000000.0 / tempoBpm_));
}

} // namespace lumena::midi
