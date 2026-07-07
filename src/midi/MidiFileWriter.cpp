#include "midi/MidiFileWriter.h"

#include <cstddef>
#include <fstream>

#include "midi/MidiSequence.h"

namespace lumena::midi {

namespace {

// MIDI channel used for every event. Lumena emits a single monophonic-ish
// melody, so channel 0 throughout is sufficient.
constexpr std::uint8_t kChannel     = 0;
constexpr std::uint8_t kNoteOnStatus  = 0x90;  // | channel
constexpr std::uint8_t kNoteOffStatus = 0x80;  // | channel
constexpr std::uint8_t kMetaPrefix    = 0xFF;

void appendBytes(std::vector<std::uint8_t>& out, const char* text,
                 std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(static_cast<std::uint8_t>(text[i]));
    }
}

void appendU16BE(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void appendU32BE(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void appendVlq(std::vector<std::uint8_t>& out, std::uint32_t value) {
    // Peel off 7 bits at a time into a little-endian scratch buffer (the low
    // 7 bits first), then emit big-endian with the continuation bit set on
    // every byte but the last. A uint32 needs at most 5 groups of 7 bits.
    std::uint8_t buffer[5];
    int count = 0;
    buffer[count++] = static_cast<std::uint8_t>(value & 0x7F);
    value >>= 7;
    while (value > 0) {
        buffer[count++] = static_cast<std::uint8_t>((value & 0x7F) | 0x80);
        value >>= 7;
    }
    for (int i = count - 1; i >= 0; --i) {
        out.push_back(buffer[i]);
    }
}

int clampMidiValue(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return value;
}

} // namespace

std::vector<std::uint8_t> MidiFileWriter::encodeVariableLengthQuantity(
    std::uint32_t value) {
    std::vector<std::uint8_t> out;
    appendVlq(out, value);
    return out;
}

std::vector<std::uint8_t> MidiFileWriter::toBytes(const MidiSequence& sequence) {
    std::vector<std::uint8_t> bytes;

    // ---- Header chunk (MThd) ------------------------------------------------
    appendBytes(bytes, "MThd", 4);
    appendU32BE(bytes, 6);  // header length is always 6
    appendU16BE(bytes, 0);  // format 0: a single multi-channel track
    appendU16BE(bytes, 1);  // one track
    appendU16BE(bytes, static_cast<std::uint16_t>(sequence.ppq()));  // division

    // ---- Track data ---------------------------------------------------------
    // Built into a scratch buffer first so its exact byte length can be written
    // into the MTrk header.
    std::vector<std::uint8_t> track;

    // Tempo meta event at delta 0: FF 51 03 tttttt (microseconds per quarter).
    const std::uint32_t mpq = sequence.microsecondsPerQuarter();
    appendVlq(track, 0);
    track.push_back(kMetaPrefix);
    track.push_back(0x51);
    track.push_back(0x03);
    track.push_back(static_cast<std::uint8_t>((mpq >> 16) & 0xFF));
    track.push_back(static_cast<std::uint8_t>((mpq >> 8) & 0xFF));
    track.push_back(static_cast<std::uint8_t>(mpq & 0xFF));

    // Note events, each with a delta time relative to the previous event.
    std::uint32_t lastTick = 0;
    for (const TimedEvent& event : sequence.events()) {
        const std::uint32_t delta = event.tick - lastTick;
        lastTick = event.tick;

        appendVlq(track, delta);
        const std::uint8_t status =
            (event.type == MidiEventType::NoteOn ? kNoteOnStatus
                                                 : kNoteOffStatus) |
            kChannel;
        track.push_back(status);
        track.push_back(static_cast<std::uint8_t>(clampMidiValue(event.noteNumber)));
        track.push_back(static_cast<std::uint8_t>(clampMidiValue(event.velocity)));
    }

    // End-of-track meta event at delta 0: FF 2F 00.
    appendVlq(track, 0);
    track.push_back(kMetaPrefix);
    track.push_back(0x2F);
    track.push_back(0x00);

    // ---- Track chunk (MTrk) -------------------------------------------------
    appendBytes(bytes, "MTrk", 4);
    appendU32BE(bytes, static_cast<std::uint32_t>(track.size()));
    bytes.insert(bytes.end(), track.begin(), track.end());

    return bytes;
}

bool MidiFileWriter::write(const MidiSequence& sequence,
                           const std::string& path) {
    const std::vector<std::uint8_t> bytes = toBytes(sequence);

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

} // namespace lumena::midi
