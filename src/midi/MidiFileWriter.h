#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lumena::midi {

class MidiSequence;

/// Serialises a MidiSequence as a Standard MIDI File (SMF), format 0.
///
/// The output is a single track containing a tempo meta event, note-on/note-off
/// events with variable-length-quantity (VLQ) delta times, and an end-of-track
/// meta event. Everything is emitted by hand — Lumena has no MIDI-library
/// dependency — so the encoding lives here and is covered directly by tests.
///
/// All members are static; the writer holds no state.
class MidiFileWriter {
public:
    /// Serialises `sequence` and writes it to `path`. Returns false if the file
    /// cannot be opened or a write fails; true on success.
    static bool write(const MidiSequence& sequence, const std::string& path);

    /// Serialises `sequence` to an in-memory byte buffer, so a host (e.g. the
    /// JUCE plugin) can obtain the MIDI data for drag-and-drop export without
    /// touching the filesystem.
    static std::vector<std::uint8_t> toBytes(const MidiSequence& sequence);

    /// Encodes `value` as a MIDI variable-length quantity: base-128, big-endian,
    /// with the high bit set on every byte except the last. Exposed for testing.
    static std::vector<std::uint8_t> encodeVariableLengthQuantity(
        std::uint32_t value);
};

} // namespace lumena::midi
