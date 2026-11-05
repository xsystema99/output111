#pragma once

#include <string>

#include <chordcore/Progression.h>
#include <chordcore/Theory.h>

namespace chordcore {

// Chord-level editing rules behind the palette, the type buttons and the key/scale selectors, plus the
// display names. JUCE-free and unit-tested; the UI only calls these.

/// Quality of the triad built on `degree` using only the scale's own notes.
///   major: I ii iii IV V vi vii°      natural minor: i ii° III iv v VI VII
/// Throws std::invalid_argument for a degree outside 1..7.
Quality diatonicQuality(Mode mode, int degree);

/// Seventh that is native to that degree's scale notes.
///   major: Imaj7 ii7 iii7 IVmaj7 V7 vi7 viiø7      natural minor: i7 iiø7 IIImaj7 iv7 v7 VImaj7 VII7
Seventh diatonicSeventh(Mode mode, int degree);

/// The plain (root position, no suspension) diatonic triad on a degree.
ChordSpec diatonicChord(Mode mode, int degree);

/// True if the chord's root is the scale degree itself (no accidental) and its quality is the diatonic one.
/// Seventh, suspension and inversion are not considered.
bool isDiatonic(Mode mode, const ChordSpec& chord);

/// Palette click: the chord on `degree`, diatonic quality, no accidental. Keeps the old chord's "type": a
/// seventh stays a seventh (the new degree's diatonic one), a suspension stays, the inversion is kept where
/// it still exists.
ChordSpec withDegree(Mode mode, const ChordSpec& chord, int degree);

/// "Triad" button: removes the seventh and the suspension (the inversion is clamped to a triad's range).
ChordSpec asTriad(const ChordSpec& chord);

/// "7th" button: adds the diatonic seventh of the chord's degree, or removes the seventh if there is one.
/// A chord whose quality is not the diatonic one still gets that degree's diatonic seventh.
ChordSpec withSeventhToggled(Mode mode, const ChordSpec& chord);

/// "Sus2"/"Sus4" button: sets the suspension, or clears it if it is already `suspension`.
/// Only major and minor chords can be suspended (see canSuspend); otherwise the chord is returned unchanged.
ChordSpec withSuspensionToggled(const ChordSpec& chord, Suspension suspension);

/// Diminished and augmented chords have no third to replace in a way that reads as a suspension.
bool canSuspend(const ChordSpec& chord);

/// Switches the progression's scale (major <-> minor, same tonic). Chords that are diatonic to the old scale
/// (including a diatonic seventh) follow the new scale; borrowed or altered chords are left as they are, so
/// a deliberate V in minor stays V. No-op if the mode does not change.
void changeMode(Progression& progression, Mode newMode);

/// Note name using flats in flat keys and sharps otherwise (decided by the relative major's key signature;
/// F#/Gb counts as sharps). `pitchClass` is taken modulo 12.
std::string noteName(const Key& key, int pitchClass);

/// Display name of the chord in that key: "F", "Bb", "Dm", "G7", "Cmaj7", "Bm7b5", "Dsus4", "G7sus4", "C/E".
std::string chordName(const Key& key, const ChordSpec& chord);

}  // namespace chordcore
