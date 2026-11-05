#pragma once

#include <cstdint>

#include <chordcore/Types.h>

namespace chordcore {

/// How note colours are assigned (see Harmony.h for the rules).
enum class ColourMapping { DiatonicCentric, MajorCentric };
enum class PaletteId { Classic, Soft, ColourBlindSafe };

/// What the piano roll writes inside a note.
enum class LabelMode { None, Pitch, Degree, Both };

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
};

inline bool operator==(const MelodySettings& a, const MelodySettings& b) noexcept {
    return a.mapping == b.mapping && a.palette == b.palette && a.labels == b.labels && a.chordChannel == b.chordChannel &&
           a.melodyChannel == b.melodyChannel && a.muteChords == b.muteChords && a.muteMelody == b.muteMelody;
}
inline bool operator!=(const MelodySettings& a, const MelodySettings& b) noexcept { return !(a == b); }

}  // namespace chordcore
