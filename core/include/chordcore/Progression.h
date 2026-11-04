#pragma once

#include <string>
#include <vector>

#include <chordcore/Theory.h>
#include <chordcore/Types.h>

namespace chordcore {

/// One chord placed on the timeline (a single lane; blocks must not overlap).
struct ChordBlock {
    ChordSpec chord;
    Tick startTick = 0;
    Tick durationTicks = 0;

    Tick endTick() const noexcept { return startTick + durationTicks; }
};

/// The progression model. A plain, copyable value type with no UI or JUCE dependency, so it can be
/// snapshotted for undo/redo and serialised later.
struct Progression {
    Key key;
    double tempoBpm = 120.0;
    TimeSignature timeSignature;
    std::vector<ChordBlock> blocks;

    /// End of the last chord (0 if there are no blocks).
    Tick endTick() const;

    /// endTick() rounded up to a whole number of bars: the length of an exported clip.
    Tick lengthTicks() const;

    /// Returns a list of problems; empty means the progression is valid.
    std::vector<std::string> validate() const;
};

/// C major, I - V - vi - IV, one 4/4 bar each, 120 BPM. Used by tests, the CLI demo and the plugin shell.
Progression makeDemoProgression();

}  // namespace chordcore
