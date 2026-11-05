#pragma once

#include <string>
#include <vector>

namespace chordcore {

enum class Mode { Major, Minor };  ///< Minor = natural minor

struct Key {
    int tonic = 0;  ///< pitch class 0..11, C = 0
    Mode mode = Mode::Major;

    bool isValid() const noexcept { return tonic >= 0 && tonic <= 11; }
};

inline bool operator==(const Key& a, const Key& b) noexcept { return a.tonic == b.tonic && a.mode == b.mode; }
inline bool operator!=(const Key& a, const Key& b) noexcept { return !(a == b); }

enum class Quality { Major, Minor, Diminished, Augmented };

/// Seventh above the root. Value = interval in semitones: Minor 10, Major 11, Diminished 9.
enum class Seventh { None, Minor, Major, Diminished };

/// Suspension: replaces the chord's third. Sus2 = major second (2 semitones), Sus4 = perfect fourth (5).
/// The fifth still follows the chord quality; a seventh may be combined (e.g. V7sus4).
enum class Suspension { None, Sus2, Sus4 };

/// Extensions stacked on a minor or major seventh. Each value is the interval in semitones above the root:
///   Ninth:      Minor 13 (flat 9), Major 14, Sharp 15 (sharp 9)
///   Eleventh:   Perfect 17, Sharp 18 (sharp 11)
///   Thirteenth: Minor 20 (flat 13), Major 21
/// An eleventh or thirteenth needs a ninth below it, and a natural 11 and a natural 13 do not combine (a "13"
/// chord leaves the 11 out). Chords with an 11th or 13th are root position only.
enum class Ninth { None, Minor, Major, Sharp };
enum class Eleventh { None, Perfect, Sharp };
enum class Thirteenth { None, Minor, Major };

/// A chord described relative to a key (key-independent until resolved with voiceChord()).
struct ChordSpec {
    int degree = 1;      ///< scale degree 1..7
    int accidental = 0;  ///< -1 = flat, 0 = none, +1 = sharp (shifts the root from the scale degree)
    Quality quality = Quality::Major;
    Seventh seventh = Seventh::None;
    int inversion = 0;   ///< 0 = root position; see maxInversion(): 2 for triads and ninth chords, 3 for seventh chords
    Suspension suspension = Suspension::None;
    Ninth ninth = Ninth::None;  ///< needs a minor or major seventh
    Eleventh eleventh = Eleventh::None;        ///< needs a ninth
    Thirteenth thirteenth = Thirteenth::None;  ///< needs a ninth

    bool isValid() const noexcept;
};

/// Highest inversion the chord supports: 2 for a triad, 3 for a seventh chord, 2 for a ninth chord (a third
/// inversion of a ninth chord could pass MIDI 127 at the extreme octave settings), 0 with an 11th or 13th.
int maxInversion(const ChordSpec& chord) noexcept;

inline bool operator==(const ChordSpec& a, const ChordSpec& b) noexcept {
    return a.degree == b.degree && a.accidental == b.accidental && a.quality == b.quality &&
           a.seventh == b.seventh && a.inversion == b.inversion && a.suspension == b.suspension &&
           a.ninth == b.ninth && a.eleventh == b.eleventh && a.thirteenth == b.thirteenth;
}
inline bool operator!=(const ChordSpec& a, const ChordSpec& b) noexcept { return !(a == b); }

struct VoicingOptions {
    /// MIDI note of the pitch class C that chord roots are placed above: root = rootBase + pitchClass.
    /// Default 48 puts roots in MIDI 48..59 (C3..B3 in the "C3 = 60" naming used by Ableton: C2..B2).
    int rootBase = 48;
};

/// Chord-symbol text of the extensions, shared by chord names and Roman numerals: the highest natural extension
/// (7, 9, 11 or 13) followed by the alterations b9 / #9, #11, b13, with "maj" in front for a major seventh:
/// "9", "maj9", "7b9", "13#11", "9b13", "11". Empty when the chord has no extension.
std::string extensionSymbol(const ChordSpec& chord);

/// Semitones above the tonic of a scale degree (1..7) in the given mode. Throws std::invalid_argument.
int scaleDegreeSemitones(Mode mode, int degree);

/// Pitch class (0..11) of the chord root in the given key.
int chordRootPitchClass(const Key& key, const ChordSpec& chord);

/// Intervals of the chord in root position, ascending, starting at 0 (e.g. {0,4,7,10}).
std::vector<int> chordIntervals(const ChordSpec& chord);

/// MIDI pitches of the chord, ascending (sorted after inversion), with the requested inversion applied.
/// Throws std::invalid_argument for an invalid key/chord and std::out_of_range if any pitch
/// would fall outside 0..127. Deterministic: same input, same output.
std::vector<int> voiceChord(const Key& key, const ChordSpec& chord, const VoicingOptions& voicing = {});

}  // namespace chordcore
