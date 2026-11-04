#pragma once

#include <vector>

#include <chordcore/Progression.h>
#include <chordcore/Theory.h>
#include <chordcore/Types.h>

namespace chordcore {

/// A single note on the timeline. The shared currency between playback and file export.
struct NoteEvent {
    int pitch = 60;         ///< MIDI note 0..127
    Tick startTick = 0;
    Tick lengthTicks = 1;   ///< >= 1
    int velocity = 96;      ///< 1..127
    int channel = 0;        ///< 0..15 (MIDI channel 1..16)
};

inline bool operator==(const NoteEvent& a, const NoteEvent& b) noexcept {
    return a.pitch == b.pitch && a.startTick == b.startTick && a.lengthTicks == b.lengthTicks &&
           a.velocity == b.velocity && a.channel == b.channel;
}
inline bool operator!=(const NoteEvent& a, const NoteEvent& b) noexcept { return !(a == b); }

struct GenerationOptions {
    VoicingOptions voicing;
    int velocity = 96;
    int channel = 0;
    /// Ticks shaved off the end of every note (articulation gap). 0 = notes last the full chord length.
    Tick releaseGapTicks = 0;
};

/// THE note generator: every chord-to-note conversion in the project goes through this function
/// (file export now, transport playback later). Pure and deterministic. The result is sorted by
/// (startTick, pitch). Throws std::invalid_argument if the progression or options are invalid.
std::vector<NoteEvent> generateNotes(const Progression& progression, const GenerationOptions& options = {});

}  // namespace chordcore
