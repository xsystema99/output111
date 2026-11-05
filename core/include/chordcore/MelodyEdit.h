#pragma once

#include <cstddef>
#include <vector>

#include <chordcore/Melody.h>
#include <chordcore/Progression.h>
#include <chordcore/Timeline.h>  // OpResult, snapToUnit, maxEndTick

namespace chordcore {

// Everything the piano roll does to the melody lane, JUCE-free and unit-tested (same contract as Timeline.h).
//
//  - Notes stay sorted by (startTick, pitch); every operation re-sorts and reports the notes' NEW indices in
//    OpResult.indices (index = the first/only affected note).
//  - Same-pitch notes never overlap (different pitches may overlap freely: it is a piano roll).
//  - An operation that cannot be done returns ok == false and leaves the progression untouched.
//  - Lock only protects a note from the generator; manual edits are always allowed.

/// Index of the note of `pitch` covering `tick` (start <= tick < end), or -1.
int findMelodyNoteAt(const Progression& p, Tick tick, int pitch);

/// Inserts a note at the snapped tick. Duration `duration` ticks (0 = one grid unit, at least 1), shortened to
/// stop before the next same-pitch note; fails if the snapped start lies inside a same-pitch note, the pitch is
/// outside 0..127 or the note would pass the end limit. Velocity is clamped to 1..127.
OpResult addMelodyNote(Progression& p, int pitch, Tick atTick, Tick unit, Tick duration = 0, int velocity = 90);

/// Moves the notes together. The time shift puts note `anchor` on the snapped `desiredAnchorStart` (clamped so no
/// note starts before 0 or passes the end limit); `pitchDelta` shifts every pitch (clamped so all stay in 0..127).
/// Fails, changing nothing, if a moved note would overlap an unmoved note of the same pitch.
OpResult moveMelodyNotes(Progression& p, const std::vector<std::size_t>& indices, std::size_t anchor,
                         Tick desiredAnchorStart, int pitchDelta, Tick unit);

/// Moves a note's right edge (start stays). Never below one grid unit unless the note is already shorter, and never
/// into the next same-pitch note or past the end limit.
OpResult resizeMelodyNoteEnd(Progression& p, std::size_t index, Tick desiredEnd, Tick unit);

/// Moves a note's left edge (end stays); same limits mirrored (previous same-pitch note, tick 0).
OpResult resizeMelodyNoteStart(Progression& p, std::size_t index, Tick desiredStart, Tick unit);

OpResult deleteMelodyNotes(Progression& p, const std::vector<std::size_t>& indices);

/// Copies the notes as one group placed right after it (the earliest copy starts where the group ends). Fails if a
/// copy would overlap or pass the end limit. Copies keep velocity; they are not locked.
OpResult duplicateMelodyNotes(Progression& p, const std::vector<std::size_t>& indices);

/// Pastes a clipboard group: its earliest note lands on the snapped `atTick`. Fails on overlap / end limit / bad pitch.
OpResult pasteMelodyNotes(Progression& p, const std::vector<MelodyNote>& clip, Tick atTick, Tick unit);

/// Velocity 1..127 (clamped) for the given notes.
OpResult setMelodyVelocity(Progression& p, const std::vector<std::size_t>& indices, int velocity);

OpResult setMelodyLocked(Progression& p, const std::vector<std::size_t>& indices, bool locked);

/// The selected notes as a clipboard group (copy).
std::vector<MelodyNote> melodyClipboard(const Progression& p, const std::vector<std::size_t>& indices);

}  // namespace chordcore
