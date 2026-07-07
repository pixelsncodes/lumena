#include "midi/MidiSequence.h"

namespace lumena::midi {

void MidiSequence::add(const NoteEvent& event) {
    events_.push_back(event);
}

void MidiSequence::clear() noexcept {
    events_.clear();
}

} // namespace lumena::midi
