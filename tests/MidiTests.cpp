// Unit tests for the MIDI stage: beat-to-tick conversion and event ordering
// (MidiSequence) and Standard MIDI File serialisation (MidiFileWriter).
//
// The byte-exact expectations here are hand-computed in the comments so the
// serialiser can be verified without a MIDI library to compare against.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "midi/MidiFileWriter.h"
#include "midi/MidiSequence.h"
#include "test_util.h"

namespace {

using lumena::midi::MidiEventType;
using lumena::midi::MidiFileWriter;
using lumena::midi::MidiSequence;
using lumena::midi::Note;

bool bytesEqual(const std::vector<std::uint8_t>& got,
                const std::vector<std::uint8_t>& want) {
    if (got.size() != want.size()) return false;
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (got[i] != want[i]) return false;
    }
    return true;
}

// ---- VLQ encoding against known values ------------------------------------

void test_vlq_known_values() {
    // 0 -> 0x00
    CHECK(bytesEqual(MidiFileWriter::encodeVariableLengthQuantity(0), {0x00}));
    // 127 -> 0x7F (largest single-byte value)
    CHECK(bytesEqual(MidiFileWriter::encodeVariableLengthQuantity(127), {0x7F}));
    // 128 -> 0x81 0x00 (classic two-byte roll-over)
    CHECK(bytesEqual(MidiFileWriter::encodeVariableLengthQuantity(128),
                     {0x81, 0x00}));
    // 100000 -> 0x86 0x8D 0x20 (the canonical three-byte example)
    CHECK(bytesEqual(MidiFileWriter::encodeVariableLengthQuantity(100000),
                     {0x86, 0x8D, 0x20}));

    // A few more anchors around byte boundaries.
    CHECK(bytesEqual(MidiFileWriter::encodeVariableLengthQuantity(0x2000),
                     {0xC0, 0x00}));      // 8192
    CHECK(bytesEqual(MidiFileWriter::encodeVariableLengthQuantity(0x3FFF),
                     {0xFF, 0x7F}));      // 16383, largest two-byte value
    CHECK(bytesEqual(MidiFileWriter::encodeVariableLengthQuantity(0x0FFFFFFF),
                     {0xFF, 0xFF, 0xFF, 0x7F}));  // largest four-byte value
}

// ---- header bytes are exactly right ---------------------------------------

void test_header_bytes() {
    const MidiSequence seq({}, 120.0, 480);
    const std::vector<std::uint8_t> bytes = MidiFileWriter::toBytes(seq);

    // MThd, length 6, format 0, 1 track, 480 PPQ (0x01E0).
    const std::vector<std::uint8_t> expectedHeader = {
        'M', 'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06,
        0x00, 0x00,  // format 0
        0x00, 0x01,  // one track
        0x01, 0xE0,  // division = 480
    };
    CHECK(bytes.size() >= expectedHeader.size());
    bool headerMatches = true;
    for (std::size_t i = 0; i < expectedHeader.size(); ++i) {
        if (bytes[i] != expectedHeader[i]) headerMatches = false;
    }
    CHECK(headerMatches);

    // A different PPQ lands in the division field verbatim (96 = 0x0060).
    const MidiSequence seq96({}, 120.0, 96);
    const std::vector<std::uint8_t> bytes96 = MidiFileWriter::toBytes(seq96);
    CHECK(bytes96[12] == 0x00);
    CHECK(bytes96[13] == 0x60);
}

// ---- a simple 2-note sequence produces the exact expected byte stream ------

void test_two_note_exact_stream() {
    // Two back-to-back quarter notes at 120 BPM, PPQ 480:
    //   note 60 (vel 100) at beat 0, length 1  -> on @ tick 0,  off @ tick 480
    //   note 64 (vel 80)  at beat 1, length 1  -> on @ tick 480, off @ tick 960
    std::vector<Note> notes = {
        {60, 100, 0.0, 1.0},
        {64, 80, 1.0, 1.0},
    };
    const MidiSequence seq(notes, 120.0, 480);
    const std::vector<std::uint8_t> bytes = MidiFileWriter::toBytes(seq);

    // Hand-computed expected stream.
    //
    // Header (14 bytes):
    //   4D 54 68 64  00 00 00 06  00 00  00 01  01 E0
    //
    // Track data (29 bytes):
    //   00 FF 51 03 07 A1 20   tempo meta, 500000 us/qn (0x07A120), delta 0
    //   00 90 3C 64            note-on  60 vel 100, delta 0
    //   83 60 80 3C 00         note-off 60,         delta 480 (VLQ 83 60)
    //   00 90 40 50            note-on  64 vel 80,  delta 0 (same tick as off)
    //   83 60 80 40 00         note-off 64,         delta 480
    //   00 FF 2F 00            end of track,        delta 0
    //
    // MTrk header: 4D 54 72 6B  00 00 00 1D  (length 29 = 0x1D)
    const std::vector<std::uint8_t> expected = {
        // MThd
        0x4D, 0x54, 0x68, 0x64, 0x00, 0x00, 0x00, 0x06,
        0x00, 0x00, 0x00, 0x01, 0x01, 0xE0,
        // MTrk
        0x4D, 0x54, 0x72, 0x6B, 0x00, 0x00, 0x00, 0x1D,
        // track data
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,
        0x00, 0x90, 0x3C, 0x64,
        0x83, 0x60, 0x80, 0x3C, 0x00,
        0x00, 0x90, 0x40, 0x50,
        0x83, 0x60, 0x80, 0x40, 0x00,
        0x00, 0xFF, 0x2F, 0x00,
    };

    CHECK(bytes.size() == expected.size());
    CHECK(bytesEqual(bytes, expected));
}

// ---- note-off precedes note-on at equal ticks -----------------------------

void test_note_off_before_note_on_at_equal_tick() {
    // Note A ends exactly where note B begins (both at tick 480). The event at
    // that tick must be the note-off, then the note-on.
    std::vector<Note> notes = {
        {60, 100, 0.0, 1.0},  // off @ 480
        {67, 100, 1.0, 1.0},  // on  @ 480
    };
    const MidiSequence seq(notes, 120.0, 480);
    const auto& events = seq.events();

    // Find the two events at tick 480 and confirm their order.
    bool sawOffThenOn = false;
    for (std::size_t i = 0; i + 1 < events.size(); ++i) {
        if (events[i].tick == 480 && events[i + 1].tick == 480) {
            sawOffThenOn = events[i].type == MidiEventType::NoteOff &&
                           events[i + 1].type == MidiEventType::NoteOn;
            break;
        }
    }
    CHECK(sawOffThenOn);

    // Globally: events are non-decreasing in tick, and never a note-on before a
    // note-off sharing its tick.
    bool ordered = true;
    for (std::size_t i = 0; i + 1 < events.size(); ++i) {
        if (events[i].tick > events[i + 1].tick) ordered = false;
        if (events[i].tick == events[i + 1].tick &&
            events[i].type == MidiEventType::NoteOn &&
            events[i + 1].type == MidiEventType::NoteOff) {
            ordered = false;
        }
    }
    CHECK(ordered);
}

// ---- tempo conversion ------------------------------------------------------

void test_tempo_microseconds() {
    CHECK(MidiSequence({}, 120.0, 480).microsecondsPerQuarter() == 500000u);
    CHECK(MidiSequence({}, 60.0, 480).microsecondsPerQuarter() == 1000000u);
    CHECK(MidiSequence({}, 240.0, 480).microsecondsPerQuarter() == 250000u);

    // Non-positive tempo/PPQ fall back to defaults (120 BPM, 480 PPQ).
    const MidiSequence bad({}, 0.0, 0);
    CHECK(bad.microsecondsPerQuarter() == 500000u);
    CHECK(bad.ppq() == 480);
}

// ---- degenerate notes are dropped -----------------------------------------

void test_degenerate_notes_dropped() {
    std::vector<Note> notes = {
        {60, 100, 0.0, 0.0},   // zero length -> dropped
        {62, 100, 1.0, -1.0},  // negative length -> dropped
        {64, 100, 2.0, 1.0},   // valid -> two events
    };
    const MidiSequence seq(notes, 120.0, 480);
    CHECK(seq.size() == 2);
    CHECK(seq.events().front().noteNumber == 64);
}

// ---- file round-trip -------------------------------------------------------

void test_file_round_trip() {
    std::vector<Note> notes = {
        {60, 100, 0.0, 1.0},
        {64, 80, 1.0, 1.0},
    };
    const MidiSequence seq(notes, 120.0, 480);

    const std::string path =
        std::string(LUMENA_TEST_TMP_DIR) + "/lumena_roundtrip.mid";
    CHECK(MidiFileWriter::write(seq, path));

    // Read the file back and compare against toBytes for the same sequence.
    std::ifstream in(path, std::ios::binary);
    CHECK(static_cast<bool>(in));
    const std::vector<std::uint8_t> fileBytes(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    in.close();

    const std::vector<std::uint8_t> memBytes = MidiFileWriter::toBytes(seq);
    CHECK(bytesEqual(fileBytes, memBytes));

    // Structural checks on the bytes read from disk.
    CHECK(fileBytes.size() >= 22);
    const bool mthd = fileBytes[0] == 'M' && fileBytes[1] == 'T' &&
                      fileBytes[2] == 'h' && fileBytes[3] == 'd';
    const bool mtrk = fileBytes[14] == 'M' && fileBytes[15] == 'T' &&
                      fileBytes[16] == 'r' && fileBytes[17] == 'k';
    CHECK(mthd);
    CHECK(mtrk);
    // Ends with the end-of-track meta event.
    const std::size_t n = fileBytes.size();
    CHECK(fileBytes[n - 3] == 0xFF && fileBytes[n - 2] == 0x2F &&
          fileBytes[n - 1] == 0x00);

    std::remove(path.c_str());

    // Writing to an unopenable path fails cleanly rather than throwing.
    CHECK(!MidiFileWriter::write(seq, "/lumena/definitely/missing/dir/x.mid"));
}

}  // namespace

void run_midi_tests() {
    test_vlq_known_values();
    test_header_bytes();
    test_two_note_exact_stream();
    test_note_off_before_note_on_at_equal_tick();
    test_tempo_microseconds();
    test_degenerate_notes_dropped();
    test_file_round_trip();
}
