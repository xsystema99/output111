#pragma once

#include <cstdint>

namespace chordcore {

/// Chromatic Dice settings (kept in their own header so Progression can store them without a circular include).
/// See ChromaticDice.h for what they do.

enum class DiceMode { PreserveRoot, ReplaceHarmony };
enum class DiceComplexity { Conservative, Adventurous, Unrestricted };

/// The nine parallel modes the dice borrow from.
enum class ParallelMode { Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian, HarmonicMinor, MelodicMinor };
constexpr int kParallelModeCount = 9;

/// Source Mode filter: Any, or one of the parallel modes (value = ParallelMode + 1).
enum class ModeFilter { Any, Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian, HarmonicMinor, MelodicMinor };

struct DiceSettings {
    DiceMode mode = DiceMode::PreserveRoot;
    DiceComplexity complexity = DiceComplexity::Conservative;
    ModeFilter sourceFilter = ModeFilter::Any;
    bool avoidRepeats = true;   ///< never roll the chord sound of the neighbouring chords (or a recent roll)
    bool rootLock = false;      ///< keep the root pitch class whatever the mode says (acts as Preserve Root)
    std::uint32_t seed = 1;     ///< deterministic seed
    std::uint32_t rollIndex = 0;  ///< advanced by every roll / reroll, so Reroll gives a new but repeatable result

    bool keepsRoot() const noexcept { return rootLock || mode == DiceMode::PreserveRoot; }
};

inline bool operator==(const DiceSettings& a, const DiceSettings& b) noexcept {
    return a.mode == b.mode && a.complexity == b.complexity && a.sourceFilter == b.sourceFilter &&
           a.avoidRepeats == b.avoidRepeats && a.rootLock == b.rootLock && a.seed == b.seed && a.rollIndex == b.rollIndex;
}
inline bool operator!=(const DiceSettings& a, const DiceSettings& b) noexcept { return !(a == b); }

}  // namespace chordcore
