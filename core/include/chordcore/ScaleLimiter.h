#pragma once

#include <cstddef>
#include <vector>

#include <chordcore/Progression.h>
#include <chordcore/Timeline.h>  // OpResult

namespace chordcore {

/// Scale Limiter: ONE switch (MelodySettings::scaleLimiter, default ON) deciding whether the melody editor works on the
/// project scale only (strict, scale-degree rows) or on all twelve pitch classes. It is an editing / generation
/// constraint, never a filter on stored data: notes outside the scale stay in Progression::melody, play and export.
///
/// Everything here is pure and JUCE-free; the piano roll only asks these functions. The scale itself comes from the
/// existing theory engine (scalePitchClassMask), so there is no second scale resolver.

/// True when the pitch belongs to the project scale (any octave).
bool pitchInScale(const Key& key, int pitch);

/// Every MIDI pitch of the scale in [low, high], ascending: the rows the piano roll shows while the limiter is ON.
/// With `limiterOn` false it returns every pitch in the range (the chromatic rows).
std::vector<int> visibleRowPitches(const Key& key, bool limiterOn, int low = 0, int high = 127);

/// Moves `pitch` by `steps` scale degrees (negative = down), clamped to the scale's pitches inside 0..127. A pitch
/// outside the scale lands on the nearest scale pitch in the direction of travel for the first step. steps == 0 is the
/// identity (also for a chromatic pitch).
int stepScalePitch(const Key& key, int pitch, int steps);

/// Number of scale degrees (pitch classes in the scale); 7 for the major and minor keys.
int scaleDegreeCount(const Key& key);

/// The pitch for scale degree `degree` (1 = tonic) closest to `referencePitch` inside [low, high] (the Smart Octave
/// rule: a key press lands in the octave nearest the previous note, never outside the register). -1 for a degree the
/// scale does not have or when no octave of it fits the register.
int pitchForScaleDegree(const Key& key, int degree, int referencePitch, int low, int high);

/// How many stored melody notes lie outside the project scale (what the limiter hides while it is ON).
int outOfScaleNoteCount(const Progression& progression);

/// Indices (ascending) of the notes the editor may show and touch: all notes when the limiter is OFF, only the
/// in-scale ones when it is ON.
std::vector<std::size_t> editableNoteIndices(const Progression& progression);

/// How many of the chord's tones are outside the project scale (e.g. Ab in Fm over C major): they are still chord
/// tones, they just have no row while the limiter is ON.
int chromaticChordToneCount(const Key& key, const ChordSpec& chord);

/// Like moveMelodyNotes but the vertical movement is `degreeDelta` scale degrees. Every moved note must be in the scale
/// (the limiter only lets such notes be selected); the group moves rigidly, clamped so all stay inside 0..127.
OpResult moveMelodyNotesByDegrees(Progression& progression, const std::vector<std::size_t>& indices, std::size_t anchor,
                                  Tick desiredAnchorStart, int degreeDelta, Tick unit);

}  // namespace chordcore
