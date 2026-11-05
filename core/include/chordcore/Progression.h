#pragma once

#include <string>
#include <vector>

#include <chordcore/Theory.h>
#include <chordcore/Types.h>

namespace chordcore {

/// Octave shifts (project-wide and per chord) are limited to +-2 octaves each, which keeps every voicing
/// inside MIDI 0..127 whatever the key, inversion or seventh.
constexpr int kMaxOctaveShift = 2;

/// Longest project length that can be requested, in bars (the editor's timeline limit).
constexpr int kMaxLengthBars = 256;

/// One chord placed on the timeline (a single lane; blocks must not overlap).
struct ChordBlock {
    ChordSpec chord;
    Tick startTick = 0;
    Tick durationTicks = 0;
    int octave = 0;  ///< this chord's own octave shift, added to Progression::octave (-2..+2)

    Tick endTick() const noexcept { return startTick + durationTicks; }
};

inline bool operator==(const ChordBlock& a, const ChordBlock& b) noexcept {
    return a.chord == b.chord && a.startTick == b.startTick && a.durationTicks == b.durationTicks && a.octave == b.octave;
}
inline bool operator!=(const ChordBlock& a, const ChordBlock& b) noexcept { return !(a == b); }

/// The progression model. A plain, copyable value type with no UI or JUCE dependency, so it can be
/// snapshotted for undo/redo and serialised later.
struct Progression {
    Key key;
    double tempoBpm = 120.0;
    TimeSignature timeSignature;
    int octave = 0;      ///< shifts every chord by this many octaves (-2..+2); 0 = the default voicing range
    int lengthBars = 0;  ///< minimum project/clip length in bars; 0 = automatic (just long enough for the chords)
    std::vector<ChordBlock> blocks;

    /// End of the last chord (0 if there are no blocks).
    Tick endTick() const;

    /// The length of an exported clip: endTick() rounded up to a whole number of bars, but never shorter than
    /// lengthBars bars when that is set (so a 4-chord idea can be exported as an 8-bar loop).
    Tick lengthTicks() const;

    /// Returns a list of problems; empty means the progression is valid.
    std::vector<std::string> validate() const;
};

inline bool operator==(const Progression& a, const Progression& b) {
    return a.key == b.key && a.tempoBpm == b.tempoBpm && a.timeSignature == b.timeSignature && a.octave == b.octave &&
           a.lengthBars == b.lengthBars && a.blocks == b.blocks;
}
inline bool operator!=(const Progression& a, const Progression& b) { return !(a == b); }

/// C major, I - V - vi - IV, one 4/4 bar each, 120 BPM. Used by tests, the CLI demo and the plugin shell.
Progression makeDemoProgression();

}  // namespace chordcore
