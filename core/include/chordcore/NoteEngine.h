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

/// Where one generated chord note came from, so a later performance stage can address voices by identity.
struct EventOrigin {
    std::size_t block = 0;  ///< index into Progression::blocks
    int voiceId = 0;        ///< VoicedNote::voiceId (stable: chord tone + doubling index, not a vector position)
    VoiceRole role = VoiceRole::Middle;
};

/// The slot between Voice Allocation and MIDI output that CHORD/GATE will fill (pipeline: Harmonic Definition ->
/// Voice Allocation -> Performance -> MIDI playback / export). No gate exists yet, so nothing implements this in the
/// product; the interface exists so the stage can be added without touching playback or export, and is tested with a
/// stand-in. A stage may change event lengths, velocities and add or remove events; `origins` runs parallel to
/// `events` on entry. It runs wherever notes are generated (never on the audio thread).
class PerformanceStage {
public:
    virtual ~PerformanceStage() = default;
    virtual void apply(const Progression& progression, const std::vector<VoicedChord>& voiced, std::vector<NoteEvent>& events,
                       std::vector<EventOrigin>& origins) const = 0;
};

struct GenerationOptions {
    VoicingOptions voicing;
    int velocity = 96;
    int channel = 0;
    /// Ticks shaved off the end of every note (articulation gap). 0 = notes last the full chord length.
    Tick releaseGapTicks = 0;
    /// true = ignore every Chord Density setting and use the original voicing (for "Original Chords" export / A-B checks).
    bool originalVoicing = false;
    /// Optional performance stage (CHORD/GATE slot). nullptr = none, the notes are exactly the voiced chords.
    const PerformanceStage* performance = nullptr;
};

/// The voicing of every block (same order as progression.blocks), after Chord Density: the single place that decides
/// which pitches a chord plays. Voice leading chains through the blocks in time order. Throws std::invalid_argument for
/// an invalid progression. generateNotes() is built on it, so playback, Preview, audition and export all agree.
std::vector<VoicedChord> voiceProgression(const Progression& progression, const VoicingOptions& voicing = {},
                                          bool originalVoicing = false);

/// THE note generator: every chord-to-note conversion in the project goes through this function
/// (file export now, transport playback later). Pure and deterministic. The result is sorted by
/// (startTick, pitch). Throws std::invalid_argument if the progression or options are invalid.
std::vector<NoteEvent> generateNotes(const Progression& progression, const GenerationOptions& options = {});

/// The melody lane as note events on the melody channel (Progression::melodySettings.melodyChannel). Mute is NOT
/// applied here; generateProjectNotes() decides what is audible. Sorted by (startTick, pitch).
std::vector<NoteEvent> generateMelodyNotes(const Progression& progression);

/// Everything the project plays: chords on the chord channel (options.channel is replaced by
/// melodySettings.chordChannel) plus the melody on the melody channel, minus whichever part is muted. This is the
/// single source for transport playback, Preview and the combined export. Sorted by (startTick, pitch, channel).
std::vector<NoteEvent> generateProjectNotes(const Progression& progression, GenerationOptions options = {});

}  // namespace chordcore
