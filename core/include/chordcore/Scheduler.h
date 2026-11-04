#pragma once

#include <cstddef>
#include <vector>

#include <chordcore/NoteEngine.h>
#include <chordcore/Types.h>

namespace chordcore {

/// What the host tells the plugin about one audio block. Plain data so tests can synthesise any transport.
struct TransportBlock {
    bool playing = false;
    double ppqStart = 0.0;     ///< host position of the first sample, in quarter notes (may be negative: count-in)
    double bpm = 120.0;        ///< host tempo for this block
    double sampleRate = 48000.0;
    int numSamples = 0;
};

/// A MIDI event positioned inside the current block.
struct ScheduledEvent {
    int sampleOffset = 0;  ///< 0 .. numSamples-1
    bool noteOn = false;
    int pitch = 60;
    int velocity = 0;      ///< 0 for note-offs
    int channel = 0;       ///< 0..15
};

/// Turns the note engine's NoteEvents into per-block MIDI events that follow the host transport.
///
/// This is the playback half of the "one engine" rule: it consumes exactly the NoteEvent list that the
/// MIDI file export is built from, so playback and exported files cannot disagree.
///
/// Position model: note ticks are compared directly with the host's quarter-note position
/// (tick = ppq * 960). Tick 0 is host position 0, independent of the host's bar/time-signature layout.
///
/// Behaviour:
///  - notes start/stop at sample offsets computed from the host tempo (rounded to the nearest sample);
///  - when the transport stops, every sounding note is released at offset 0 of that block;
///  - when the position is not the continuation of the previous block (locate, loop wrap, start), all
///    sounding notes are released at offset 0 and any note that is sounding at the new position is
///    started at offset 0 (so playback can begin mid-chord);
///  - at equal sample offsets note-offs come before note-ons, so a repeated pitch re-triggers cleanly.
///
/// Limitation: a loop wrap is only noticed at the next block, so notes ending exactly at the loop end
/// are released up to one block late.
///
/// Real-time safety: setNotes() allocates and must be called off the audio thread. process() and reset()
/// never allocate. The class is not thread-safe: use it from one thread at a time.
class PlaybackScheduler {
public:
    /// Replaces the note list (and forgets which notes were sounding).
    void setNotes(std::vector<NoteEvent> notes);

    /// Forgets sounding notes WITHOUT emitting note-offs (use when the host re-initialises the plugin).
    void reset();

    /// Events for this block, sorted by (sampleOffset, note-offs first, pitch). The reference stays valid
    /// until the next call. A block with invalid parameters (no samples, non-positive tempo or sample
    /// rate) produces no events and leaves the state untouched.
    const std::vector<ScheduledEvent>& process(const TransportBlock& block);

    /// Number of notes the scheduler currently considers sounding.
    std::size_t activeNoteCount() const;

private:
    std::vector<NoteEvent> notes_;
    std::vector<char> active_;
    std::vector<ScheduledEvent> events_;
    bool wasPlaying_ = false;
    double expectedStartTick_ = 0.0;  ///< where the next contiguous block should begin
    double previousHi_ = 0.0;         ///< upper edge of the last block's tick window
};

}  // namespace chordcore
