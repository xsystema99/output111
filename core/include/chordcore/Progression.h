#pragma once

#include <optional>
#include <string>
#include <vector>

#include <chordcore/DiceSettings.h>
#include <chordcore/MidiFilenameFormat.h>
#include <chordcore/Melody.h>
#include <chordcore/Theory.h>
#include <chordcore/Types.h>
#include <chordcore/Voicing.h>

namespace chordcore {

/// Octave shifts (project-wide and per chord) are limited to +-2 octaves each. Progression::validate() also
/// rejects any chord whose voicing would leave MIDI 0..127 (only possible for 13th chords at the extreme octaves).
constexpr int kMaxOctaveShift = 2;

/// Longest project length that can be requested, in bars (the editor's timeline limit).
constexpr int kMaxLengthBars = 256;

/// One chord placed on the timeline (a single lane; blocks must not overlap).
struct ChordBlock {
    ChordSpec chord;
    Tick startTick = 0;
    Tick durationTicks = 0;
    int octave = 0;  ///< this chord's own octave shift, added to Progression::octave (-2..+2)
    /// Individual Chord Density settings; empty = follow Progression::voicing (the global settings).
    std::optional<VoicingSettings> voicing = std::nullopt;

    Tick endTick() const noexcept { return startTick + durationTicks; }
};

inline bool operator==(const ChordBlock& a, const ChordBlock& b) noexcept {
    return a.chord == b.chord && a.startTick == b.startTick && a.durationTicks == b.durationTicks && a.octave == b.octave &&
           a.voicing == b.voicing;
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
    MidiFilenameFormat exportNaming = MidiFilenameFormat::Absolute;  ///< how exported files are named
    DiceSettings dice;        ///< Chromatic Dice settings (seed, mode, complexity ...)
    VoicingSettings voicing;  ///< global Chord Density settings (default: Auto / Original)
    std::vector<MelodyNote> melody;  ///< the melody lane, kept sorted by (startTick, pitch)
    MelodySettings melodySettings;
    std::vector<MelodyBasisEntry> melodyBasis;  ///< chords the melody was last generated against (see Melody.h)

    /// The Chord Density settings that apply to block `index`: its own override, else the global settings.
    const VoicingSettings& voicingFor(std::size_t index) const { return blocks[index].voicing ? *blocks[index].voicing : voicing; }

    /// End of the last chord (0 if there are no blocks).
    Tick endTick() const;

    /// End of the last melody note (0 if there is no melody).
    Tick melodyEndTick() const;

    /// The length of an exported clip: the later of endTick() and melodyEndTick() rounded up to a whole number of bars, but never shorter than
    /// lengthBars bars when that is set (so a 4-chord idea can be exported as an 8-bar loop).
    Tick lengthTicks() const;

    /// Returns a list of problems; empty means the progression is valid.
    std::vector<std::string> validate() const;
};

inline bool operator==(const Progression& a, const Progression& b) {
    return a.key == b.key && a.tempoBpm == b.tempoBpm && a.timeSignature == b.timeSignature && a.octave == b.octave &&
           a.lengthBars == b.lengthBars && a.blocks == b.blocks && a.voicing == b.voicing && a.dice == b.dice && a.exportNaming == b.exportNaming && a.melody == b.melody &&
           a.melodySettings == b.melodySettings && a.melodyBasis == b.melodyBasis;
}
inline bool operator!=(const Progression& a, const Progression& b) { return !(a == b); }

/// C major, I - V - vi - IV, one 4/4 bar each, 120 BPM. Used by tests, the CLI demo and the plugin shell.
Progression makeDemoProgression();

}  // namespace chordcore
