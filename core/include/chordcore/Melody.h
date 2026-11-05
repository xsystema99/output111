#pragma once

#include <cstdint>

#include <chordcore/Types.h>

namespace chordcore {

/// How note colours are assigned (see Harmony.h for the rules).
enum class ColourMapping { DiatonicCentric, MajorCentric };
enum class PaletteId { Classic, Soft, ColourBlindSafe };

/// What the piano roll writes inside a note.
enum class LabelMode { None, Pitch, Degree, Both };

/// Rule-based melody generator parameters (saved with the project).
enum class Contour { Ascending, Descending, Arch, InvertedArch, Wave, Free };
enum class Density { Sparse, Moderate, Dense };
enum class RhythmStyle { Sustained, Lyrical, Rhythmic, Syncopated, Free };
/// How much the Generate button covers, counted from the start bar the editor passes in.
enum class PhraseScope { OneBar, TwoBars, FourBars, EightBars, SelectedRegion, FullProgression };

struct GeneratorSettings {
    Contour contour = Contour::Arch;
    Density density = Density::Moderate;
    int tension = 25;  ///< 0..100: 0 = chord and scale tones only, 100 = chromatic notes and leaps are welcome
    RhythmStyle rhythm = RhythmStyle::Lyrical;
    PhraseScope scope = PhraseScope::FourBars;
    int registerLow = 60;    ///< lowest MIDI pitch the generator uses (default C4)
    int registerHigh = 84;   ///< highest (default C6)
    std::uint32_t seed = 1;
};

inline bool operator==(const GeneratorSettings& a, const GeneratorSettings& b) noexcept {
    return a.contour == b.contour && a.density == b.density && a.tension == b.tension && a.rhythm == b.rhythm &&
           a.scope == b.scope && a.registerLow == b.registerLow && a.registerHigh == b.registerHigh && a.seed == b.seed;
}
inline bool operator!=(const GeneratorSettings& a, const GeneratorSettings& b) noexcept { return !(a == b); }

/// A chord block as it was when the melody over it was last generated: time span plus a hash of the chord. Lets
/// "Regenerate affected melody" find the chords that changed since. Never edited by hand.
struct MelodyBasisEntry {
    Tick startTick = 0;
    Tick durationTicks = 0;
    std::uint32_t chordHash = 0;
};
inline bool operator==(const MelodyBasisEntry& a, const MelodyBasisEntry& b) noexcept {
    return a.startTick == b.startTick && a.durationTicks == b.durationTicks && a.chordHash == b.chordHash;
}

/// Limits shared by validation, deserialisation and the editing operations.
constexpr int kMaxMelodyNotes = 4096;
constexpr int kMinMidiChannel = 1;
constexpr int kMaxMidiChannel = 16;

/// One melody note. A plain value: no UI state, no channel (the channel is a project setting, so that all melody
/// notes move together and playback/export read the same field).
struct MelodyNote {
    int pitch = 60;  ///< MIDI note 0..127
    Tick startTick = 0;
    Tick durationTicks = 0;
    int velocity = 90;  ///< 1..127
    bool locked = false;  ///< protects the note from the generator (regenerate/reroll); hand edits still work

    Tick endTick() const noexcept { return startTick + durationTicks; }
};

inline bool operator==(const MelodyNote& a, const MelodyNote& b) noexcept {
    return a.pitch == b.pitch && a.startTick == b.startTick && a.durationTicks == b.durationTicks &&
           a.velocity == b.velocity && a.locked == b.locked;
}
inline bool operator!=(const MelodyNote& a, const MelodyNote& b) noexcept { return !(a == b); }

/// Project-wide melody lane settings (saved with the project).
struct MelodySettings {
    ColourMapping mapping = ColourMapping::DiatonicCentric;
    PaletteId palette = PaletteId::Classic;
    LabelMode labels = LabelMode::Degree;
    int chordChannel = 1;   ///< MIDI channel (1..16) used by chord notes in playback and export
    int melodyChannel = 2;  ///< MIDI channel (1..16) used by melody notes
    bool muteChords = false;
    bool muteMelody = false;
    GeneratorSettings generator;
};

inline bool operator==(const MelodySettings& a, const MelodySettings& b) noexcept {
    return a.mapping == b.mapping && a.palette == b.palette && a.labels == b.labels && a.chordChannel == b.chordChannel &&
           a.melodyChannel == b.melodyChannel && a.muteChords == b.muteChords && a.muteMelody == b.muteMelody && a.generator == b.generator;
}
inline bool operator!=(const MelodySettings& a, const MelodySettings& b) noexcept { return !(a == b); }

}  // namespace chordcore
