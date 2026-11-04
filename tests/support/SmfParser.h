#pragma once

// A deliberately small, STRICT Standard MIDI File parser used only by the tests.
// It is written from the SMF specification and shares no code with chordcore's writer, so a
// round trip through it is an independent check of the bytes the writer produces.
//
// Strictness (things a lenient host might forgive, but we want to know about):
//   - chunk lengths must account for every byte of the file (no trailing garbage)
//   - every track must end with exactly one End-of-Track meta event, and it must be last
//   - the parser records whether running status was used (the writer is expected not to use it)

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace smf {

struct Event {
    std::uint64_t tick = 0;          ///< absolute tick
    bool isMeta = false;
    std::uint8_t metaType = 0;       ///< valid when isMeta
    std::uint8_t status = 0;         ///< channel-voice status byte (0x80..0xEF); valid when !isMeta
    std::vector<std::uint8_t> data;  ///< meta payload, or channel-voice data bytes
    std::size_t fileOrder = 0;       ///< position within the track (to check ordering at equal ticks)

    int channel() const { return status & 0x0F; }
    int kind() const { return status & 0xF0; }  ///< 0x80 note-off, 0x90 note-on, ...
};

struct Track {
    std::vector<Event> events;
    bool usedRunningStatus = false;
};

struct File {
    int format = -1;
    int trackCount = 0;
    int division = 0;  ///< ticks per quarter note (SMPTE division is rejected)
    std::vector<Track> tracks;
};

/// A note reconstructed by pairing a note-on with its matching note-off.
struct Note {
    int pitch = 0;
    std::uint64_t start = 0;
    std::uint64_t length = 0;
    int velocity = 0;
    int channel = 0;
};

/// Throws std::runtime_error with a descriptive message for any malformed input.
File parse(const std::vector<std::uint8_t>& bytes);

/// Pairs note-ons with note-offs (a note-on with velocity 0 counts as a note-off).
/// Throws if a note-off has no matching note-on or a note-on is never released.
/// Result sorted by (start, pitch).
std::vector<Note> extractNotes(const Track& track);

/// All meta events of a given type in a track.
std::vector<const Event*> metaEvents(const Track& track, std::uint8_t metaType);

}  // namespace smf
