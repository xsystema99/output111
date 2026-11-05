#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <chordcore/Progression.h>
#include <chordcore/Theory.h>
#include <chordcore/Types.h>

namespace chordcore {

// The one place that decides what a pitch means in a key and against a chord. JUCE-free and tested; the GUI only
// draws what this returns.
//
// Two separate visual systems (never mixed):
//   * NOTE COLOUR is identity: it depends only on the pitch, the key, the colour mapping and the palette. The chord
//     never changes it (a blue G stays blue when it is not in the chord).
//   * BACKGROUND is compatibility with the chord that is active at that moment: chord tone / scale tone / chromatic.
//
// Colour mapping rules (the palette has seven colours, one per degree of a MAJOR scale):
//   Diatonic-Centric  Colours are those of the key's parent major scale: a major key is its own parent; a natural-minor
//                     key borrows the colours of its relative major (A minor keeps C major's colours, so A is purple,
//                     C is red). A modal key would use the parent major of its mode (the mapping takes the parent
//                     tonic, so Dorian on D uses C as parent).
//   Major-Centric     Colours follow the interval above the tonic as if the tonic's own major scale applied (A minor:
//                     A is red, B orange, C is the blend for a flat third).
//   Altered degrees   A pitch outside the parent major scale gets the midpoint (RGB average, rounded half up) of the
//                     colours of the two parent-scale degrees it lies between: b2 = (1,2), b3 = (2,3), #4/b5 = (4,5),
//                     b6 = (5,6), b7 = (6,7).
//
// Degree labels: Major-Centric always counts from the tonic against the major scale: 1 b2 2 b3 3 4 #4 5 b6 6 b7 7.
// Diatonic-Centric counts against the key's own scale: major as above; natural minor 1 b2 2 3 #3 4 #4 5 6 #6 7 #7
// (so A minor's C is "3" and its G# is "#7").

struct Rgb {
    std::uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb& o) const noexcept { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const noexcept { return !(*this == o); }
};

struct Rgba {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

/// Seven colours: major-scale degrees 1..7. Classic: red, orange, gold, green, blue, purple, pink.
std::array<Rgb, 7> paletteColours(PaletteId palette);

struct HarmonicSettings {
    ColourMapping mapping = ColourMapping::DiatonicCentric;
    PaletteId palette = PaletteId::Classic;
};

/// Colour of a pitch class for a given parent-major tonic (the colour rule above, without any key logic).
Rgb colourForPitchClass(int parentMajorTonic, int pitchClass, PaletteId palette);

/// Relationship of a pitch to the key; independent of any chord.
struct PitchIdentity {
    int pitchClass = 0;           ///< absolute 0..11
    int chromaticDegree = 0;      ///< semitones above the tonic, 0..11
    int diatonicDegree = 1;       ///< 1..7: the degree the label counts from (for b3 it is 3, for #4 it is 4)
    int chromaticAlteration = 0;  ///< -1 flat, 0 natural, +1 sharp
    bool inScale = false;         ///< belongs to the project scale
    Rgb colour;                   ///< note colour
    std::string label;            ///< "1", "b3", "#4" ...
};

enum class Compat { ChordTone, ScaleTone, Chromatic };

struct HarmonicVisualInfo {
    int chromaticDegree = 0;
    int diatonicDegree = 1;
    int chromaticAlteration = 0;
    bool isChordTone = false;  ///< belongs to the active chord (extensions and alterations included)
    bool isScaleTone = false;  ///< belongs to the project scale (independent of the chord: a borrowed Ab is a chord tone and not a scale tone in C major)
    bool isChromatic = false;  ///< outside the project scale
    Compat compatibility = Compat::Chromatic;  ///< what the background shows: chord tone beats scale tone beats chromatic
    Rgb noteColour;
    Rgba backgroundColour;
    std::string label;
};

/// Pitch classes of the project scale as a 12-bit mask (bit n = pitch class n).
std::uint16_t scalePitchClassMask(const Key& key);

/// Pitch classes of the chord's actual notes (all extensions, alterations and suspensions) as a 12-bit mask.
std::uint16_t chordPitchClassMask(const Key& key, const ChordSpec& chord);

/// Precomputed per-key table: build once when the key or settings change, then look pitches up cheaply.
class HarmonyResolver {
public:
    HarmonyResolver() : HarmonyResolver(Key{}, HarmonicSettings{}) {}
    HarmonyResolver(const Key& key, const HarmonicSettings& settings);

    const Key& key() const noexcept { return key_; }
    const HarmonicSettings& settings() const noexcept { return settings_; }

    const PitchIdentity& identity(int midiPitchOrPitchClass) const noexcept;
    Compat classify(int midiPitchOrPitchClass, std::uint16_t chordMask) const noexcept;
    HarmonicVisualInfo visual(int midiPitchOrPitchClass, std::uint16_t chordMask) const;

    /// Background for a compatibility class over a note colour: chord tone = strong tint, scale tone = faint tint,
    /// chromatic = neutral (the GUI adds a pattern).
    static Rgba backgroundFor(Compat compat, const Rgb& noteColour) noexcept;

private:
    Key key_;
    HarmonicSettings settings_;
    std::array<PitchIdentity, 12> table_{};
    std::uint16_t scaleMask_ = 0;
};

// ---- compatibility over time

/// The chord that is active over [startTick, endTick): exactly the block's own ticks, never rounded.
struct CompatibilityRegion {
    Tick startTick = 0;
    Tick endTick = 0;
    std::uint16_t chordMask = 0;
    std::size_t blockIndex = 0;
};

/// One region per chord block, in time order. Gaps between chords have no region (nothing is highlighted there).
std::vector<CompatibilityRegion> buildCompatibility(const Progression& progression);

/// The region covering `tick` (start <= tick < end), or nullptr.
const CompatibilityRegion* regionAt(const std::vector<CompatibilityRegion>& regions, Tick tick) noexcept;

}  // namespace chordcore
