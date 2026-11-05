#pragma once

#include <string>
#include <vector>

#include <chordcore/Theory.h>

namespace chordcore {

/// Chord Density 2.0: a non-destructive voicing layer on top of the chord model. The chord itself (root, quality,
/// extensions, inversion) is never changed; these settings only decide which chord tones sound, in which octaves.
/// density 0 = Auto / Original: the voicing is exactly what voiceChord() returns.

enum class RegisterPreset { Low, Mid, High, Full, Custom };
enum class Distribution { Automatic, Close, Open, Spread };

constexpr int kMaxDensity = 12;

struct VoicingSettings {
    int density = 0;                                   ///< 0 = Auto/Original, otherwise 1..12 distinct sounding pitches
    RegisterPreset registerPreset = RegisterPreset::Mid;
    int customLow = 36;                                ///< Custom register, absolute MIDI notes
    int customHigh = 84;
    Distribution distribution = Distribution::Automatic;
    bool preserveBass = true;                          ///< keep the chord's bass tone (inversion bass) as the lowest voice
    bool voiceLeading = false;                         ///< pick octave placements that move least from the previous chord

    bool isAuto() const noexcept { return density == 0; }
};

inline bool operator==(const VoicingSettings& a, const VoicingSettings& b) noexcept {
    return a.density == b.density && a.registerPreset == b.registerPreset && a.customLow == b.customLow &&
           a.customHigh == b.customHigh && a.distribution == b.distribution && a.preserveBass == b.preserveBass &&
           a.voiceLeading == b.voiceLeading;
}
inline bool operator!=(const VoicingSettings& a, const VoicingSettings& b) noexcept { return !(a == b); }

/// Empty string when valid, otherwise the problem (density 0..12, custom register inside 0..127 and at least 12 semitones wide).
std::string validateVoicingSettings(const VoicingSettings& settings);

struct RegisterRange {
    int low = 0;
    int high = 127;
};

/// The absolute MIDI range of the register. Presets (Low 24-59, Mid 36-84, High 60-108) follow the chord's octave shift
/// (project octave + chord octave) and are clamped to 0..127; Full is 0..127; Custom is exactly what was entered.
RegisterRange registerRange(const VoicingSettings& settings, int octaveShift);

enum class VoiceRole { Bass, Lower, Middle, Upper, Extension };

/// One sounding pitch with the metadata a later performance stage (CHORD/GATE) can address. voiceId is stable: it
/// derives from the chord tone and its doubling index, never from the position in the vector.
struct VoicedNote {
    int pitch = 60;
    VoiceRole role = VoiceRole::Middle;
    int voiceId = 0;        ///< interval * 8 + copy index; the same chord tone keeps its id when density changes
    int interval = 0;       ///< semitones above the chord root (0..21), as in chordIntervals()
    bool original = false;  ///< this exact pitch is part of the original (Auto) voicing
    bool doubling = false;  ///< an octave doubling of another voice
};

struct VoicedChord {
    std::vector<VoicedNote> notes;  ///< ascending by pitch
    int targetVoices = 0;           ///< what the settings asked for (the original count in Auto)
    int actualVoices = 0;           ///< notes.size()
    int originalVoices = 0;         ///< voice count of the original voicing
    RegisterRange range;
    bool original = true;           ///< Auto: identical to voiceChord()
    std::string note;               ///< why actual < target, empty otherwise

    std::vector<int> pitches() const;
};

/// Voice one chord. `voicing.rootBase` already includes the octave shift; `octaveShift` is only used to place the
/// register presets. `previousPitches` (may be null) is the previous chord's sounding pitches for voice leading.
/// Pure and deterministic, bounded work, never called from the audio thread.
/// Throws what voiceChord() throws for an invalid chord or an original voicing outside 0..127.
VoicedChord voiceChordWithSettings(const Key& key, const ChordSpec& chord, const VoicingOptions& voicing,
                                   const VoicingSettings& settings, int octaveShift,
                                   const std::vector<int>* previousPitches = nullptr);

const char* toString(RegisterPreset preset);
const char* toString(Distribution distribution);
const char* toString(VoiceRole role);

}  // namespace chordcore
