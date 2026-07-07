#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lumena::midi {

/// A single note as produced by the generator: a pitch and velocity with
/// beat-based timing. This is the hand-off type between the melody pipeline
/// and the MIDI stage.
struct Note {
    int    noteNumber  = 60;    ///< MIDI note number, 0-127 (60 = middle C).
    int    velocity    = 100;   ///< MIDI velocity, 1-127.
    double startBeats  = 0.0;   ///< Onset in beats from the sequence start.
    double lengthBeats = 1.0;   ///< Duration in beats.
};

/// The two channel-voice events a note resolves into. Ordered so that, at an
/// equal tick, a NoteOff sorts before a NoteOn: releasing a pitch before a new
/// note-on at the same instant avoids a spurious retrigger being cut short.
enum class MidiEventType : int {
    NoteOff = 0,
    NoteOn  = 1,
};

/// A note-on or note-off resolved to an absolute tick on the sequence timeline.
struct TimedEvent {
    std::uint32_t tick       = 0;                    ///< Absolute tick from start.
    MidiEventType type       = MidiEventType::NoteOn;
    int           noteNumber = 60;                   ///< 0-127.
    int           velocity   = 0;                    ///< 0-127 (0 for note-offs).
};

/// Turns a list of beat-timed Notes into a tick-based event stream ready for
/// serialisation as a Standard MIDI File.
///
/// Construction converts each Note's beat onset and length into note-on and
/// note-off ticks using the tempo and pulses-per-quarter-note (PPQ) resolution,
/// then sorts the events by tick with note-offs ahead of note-ons at the same
/// tick. Degenerate notes (non-positive length) are dropped; pitches and
/// velocities are clamped to the valid MIDI range.
///
/// The class is a pure value: it holds no file or JUCE state and never throws.
class MidiSequence {
public:
    static constexpr double kDefaultTempoBpm = 120.0;
    static constexpr int    kDefaultPpq      = 480;

    /// Builds the event stream from `notes`. A non-positive `tempoBpm` or `ppq`
    /// falls back to the defaults.
    explicit MidiSequence(const std::vector<Note>& notes,
                          double tempoBpm = kDefaultTempoBpm,
                          int    ppq      = kDefaultPpq);

    /// The resolved note-on/note-off events, sorted by tick (note-offs first at
    /// equal ticks).
    const std::vector<TimedEvent>& events() const noexcept { return events_; }

    double tempoBpm() const noexcept { return tempoBpm_; }
    int    ppq() const noexcept { return ppq_; }

    /// The tempo expressed as microseconds per quarter note, as stored in the
    /// MIDI tempo meta event (e.g. 500000 for 120 BPM).
    std::uint32_t microsecondsPerQuarter() const noexcept;

    std::size_t size() const noexcept { return events_.size(); }
    bool empty() const noexcept { return events_.empty(); }

private:
    double tempoBpm_;
    int    ppq_;
    std::vector<TimedEvent> events_;
};

} // namespace lumena::midi
