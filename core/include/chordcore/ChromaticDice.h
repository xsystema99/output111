#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <chordcore/DiceSettings.h>
#include <chordcore/Progression.h>
#include <chordcore/Theory.h>

namespace chordcore {

/// Chromatic Dice 2.0: replaces one chord with a chord borrowed from the parallel modes of the PROJECT tonic (same
/// tonic, different mode) or, in Unrestricted, from any root and quality. The project tonic never changes. Pure and
/// deterministic: the same progression, block and settings always roll the same chord. Never runs on the audio thread.
///
/// Normalisation policy (documented, tested): the rolled chord keeps its block's position, duration, octave and
/// density settings. Its own extensions and alterations replace the old ones. The inversion is kept when the new chord
/// supports it, otherwise reduced to the highest inversion it supports (DiceResult::normalizationNote says so).
/// There is no slash-bass field in the chord model yet, so slash bass cannot be incompatible.

struct DiceCandidate {
    ChordSpec chord;
    int rootPitchClass = 0;
    std::vector<ParallelMode> sources;  ///< the parallel modes this exact chord sound belongs to (empty: chromatic, Unrestricted)
    int score = 0;                      ///< higher = closer to the current chord / smoother
    std::string roman;                  ///< chromatic Roman numeral relative to the project tonic, e.g. "bVImaj7", "#iv°"
    std::string name;                   ///< absolute chord name, e.g. "Abmaj7"
};

/// The scale of a parallel mode as semitones above the tonic.
std::vector<int> parallelModeScale(ParallelMode mode);
const char* toString(ParallelMode mode);
const char* toString(ModeFilter filter);
const char* toString(DiceMode mode);
const char* toString(DiceComplexity complexity);

/// The eligible, de-duplicated, scored candidates for block `blockIndex`, best first. `recent` lists chords that
/// avoidRepeats should also skip. Throws std::out_of_range for a bad index.
std::vector<DiceCandidate> diceCandidates(const Progression& progression, std::size_t blockIndex, const DiceSettings& settings,
                                          const std::vector<ChordSpec>* recent = nullptr);

struct DiceResult {
    bool ok = false;            ///< false: nothing eligible, the chord is unchanged (see message)
    ChordSpec chord;            ///< the chord to store (the original chord when !ok)
    DiceCandidate picked;
    int poolSize = 0;
    std::string message;        ///< user-facing feedback
    bool normalized = false;
    std::string normalizationNote;
};

/// Weighted random pick (not always the top-ranked candidate; Conservative favours the best, Unrestricted is flat).
/// Does not modify the progression; store `chord` with a normal undoable edit so Redo restores exactly this result.
DiceResult rollChromaticDice(const Progression& progression, std::size_t blockIndex, const DiceSettings& settings,
                             const std::vector<ChordSpec>* recent = nullptr);

/// The seed actually used for a roll (settings.seed mixed with rollIndex and the block position).
std::uint64_t diceRollSeed(const DiceSettings& settings, Tick blockStart);

}  // namespace chordcore
