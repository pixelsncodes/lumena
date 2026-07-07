#pragma once

#include <cstddef>
#include <vector>

namespace lumena::midi {

/// A single timed MIDI note event.
struct NoteEvent {
    int    noteNumber  = 60;    ///< MIDI note number, 0-127 (60 = middle C).
    int    velocity    = 100;   ///< MIDI velocity, 0-127.
    double startBeats  = 0.0;   ///< Onset in beats from the sequence start.
    double lengthBeats = 1.0;   ///< Duration in beats.
};

/// An ordered list of NoteEvents produced by the generator and later handed to
/// the JUCE layer for playback.
///
/// Skeleton only: this is a thin container; the note-generation pipeline that
/// fills it is not implemented yet.
class MidiSequence {
public:
    void add(const NoteEvent& event);
    void clear() noexcept;

    const std::vector<NoteEvent>& events() const noexcept { return events_; }
    std::size_t size() const noexcept { return events_.size(); }
    bool empty() const noexcept { return events_.empty(); }

private:
    std::vector<NoteEvent> events_;
};

} // namespace lumena::midi
