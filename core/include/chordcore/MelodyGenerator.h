#pragma once

#include <cstdint>
#include <vector>

#include <chordcore/Melody.h>
#include <chordcore/Progression.h>
#include <chordcore/Timeline.h>  // OpResult

namespace chordcore {

// Deterministic, rule-based melody generator. JUCE-free, never touches the audio thread (the editor calls it, the
// result is an ordinary edit). Same project + settings + seed + range always give the same notes, on every platform
// (it uses its own integer random generator, not <random> distributions).
//
// How a note is chosen (all weights are plain numbers in MelodyGenerator.cpp):
//   rhythm   per bar, onsets are drawn on an eighth-note grid from the density and rhythm style; Rhythmic/Lyrical
//            bars often repeat the previous bar's pattern (motif repetition).
//   pitch    every candidate in the register is scored: chord tone (more on strong beats), scale tone on weak beats,
//            chromatic notes only as tension allows, a half-step clash above a chord tone on strong beats is
//            penalised, closeness to the contour target, small intervals preferred over leaps, common tones kept
//            across chord changes, the phrase ending prefers the chord root, repeated notes discouraged. The best
//            score wins; a small seeded jitter makes different seeds differ.
//   locked   locked notes (and any note outside the range) are never touched: new notes avoid their time span and
//            treat them as melodic context.
// Chord data comes from the chords active at each onset (the same chord masks that colour the piano roll).

struct GenerationRange {
    Tick start = 0;
    Tick end = 0;
};

/// Stable hash of a chord's fields (used for the melody basis).
std::uint32_t chordHash(const ChordSpec& chord);

/// The range a scope covers: `startBar` (1-based) for the bar scopes, [selectedStart, selectedEnd) for
/// SelectedRegion (falls back to startBar + four bars when empty), the whole project for FullProgression. Always
/// whole-bar aligned for bar scopes and clipped to the end of the timeline.
GenerationRange scopeRange(const Progression& progression, PhraseScope scope, int startBar, Tick selectedStart,
                           Tick selectedEnd);

/// Pure generation: the new notes for [range.start, range.end). Existing notes are only read: any note overlapping
/// the range is an obstacle (see above). Callers wanting "replace" semantics use regenerateMelody().
std::vector<MelodyNote> generateMelodyForRange(const Progression& progression, const GeneratorSettings& settings,
                                               GenerationRange range);

/// Replaces every UNLOCKED note that starts inside the range with newly generated notes (locked notes stay) using
/// progression.melodySettings.generator, and records the chords it generated against. OpResult.indices = the new
/// notes. Fails, changing nothing, for an empty range or when the lane would overflow.
OpResult regenerateMelody(Progression& progression, GenerationRange range);

/// Spans of the timeline whose chords differ from the chords the melody was last generated against (changed,
/// added or removed chords), merged when adjacent. Empty when nothing was generated yet or nothing changed.
std::vector<GenerationRange> affectedRanges(const Progression& progression);

/// regenerateMelody() over every affected range at once. Fails with a message when nothing was generated yet or
/// nothing changed. OpResult.indices = all new notes.
OpResult regenerateAffectedMelody(Progression& progression);

}  // namespace chordcore
