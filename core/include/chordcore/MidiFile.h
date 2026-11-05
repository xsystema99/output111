#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>

namespace chordcore {

struct MidiExportOptions {
    std::string trackName = "Chords";  ///< empty = no track-name meta event
    bool writeKeySignature = true;
};

/// Appends a MIDI variable-length quantity. Throws std::out_of_range if value > 0x0FFFFFFF.
void appendVariableLengthQuantity(std::vector<std::uint8_t>& out, std::uint32_t value);

/// Key signature "sharps/flats" count for the SMF key-signature meta event: -7..+7, negative = flats.
/// Minor keys use their relative major. Spelling is chosen from -6..+6 (e.g. pitch class 6 -> +6, F# major).
int midiKeySignatureSharps(const Key& key);

/// Serialises already-generated notes plus the progression's tempo/time-signature/key into a
/// Standard MIDI File (Format 0, single track, 960 PPQ). Deterministic.
///   tick 0: track name, time signature, tempo, key signature
///   notes:  note-offs sort before note-ons at the same tick
///   end:    End-of-Track at max(progression.lengthTicks(), last note end)
/// Throws std::invalid_argument on an invalid progression or note.
std::vector<std::uint8_t> buildMidiFile(const Progression& progression, const std::vector<NoteEvent>& notes,
                                        const MidiExportOptions& options = {});

/// Convenience: generateNotes() followed by buildMidiFile(). This is the single export entry point.
std::vector<std::uint8_t> exportProgressionToMidi(const Progression& progression,
                                                  const GenerationOptions& generation = {},
                                                  const MidiExportOptions& options = {});

/// Exports bars firstBar..lastBar (1-based, inclusive) as their own file starting at tick 0: the same notes the
/// whole-project export would contain, shifted so firstBar begins at tick 0. A note that starts before the range
/// but sounds into it is kept from the range start; a note running past the range end is cut there; notes
/// wholly outside are dropped. The end-of-track is at the end of lastBar. Throws std::invalid_argument for a
/// range that is empty, starts before bar 1, or is past 256 bars.
std::vector<std::uint8_t> exportProgressionRangeToMidi(const Progression& progression, int firstBar, int lastBar,
                                                       const GenerationOptions& generation = {},
                                                       const MidiExportOptions& options = {});

// ---- chords / melody / combined export ----------------------------------------------------------------------------

enum class ExportPart { Chords, Melody, Combined };

struct ProjectExportOptions {
    ExportPart part = ExportPart::Combined;
    bool melodyOnChannel1 = false;  ///< melody written on channel 1 instead of its own channel (for hosts that ignore channels)
    int firstBar = 0;               ///< 0 / 0 = the whole project, otherwise bars first..last (1-based, inclusive)
    int lastBar = 0;
    bool writeKeySignature = true;
    /// true = chords as originally voiced, ignoring every Chord Density setting ("Original Chords.mid"); false = as played.
    bool originalVoicing = false;
};

/// Default file name for a part: "Chords.mid", "Melody.mid", "Combined.mid".
const char* exportFileName(ExportPart part);
/// Same, with the "Original " prefix when the chords are exported with their original voicing. There is no gated
/// export yet ("Gated Chords.mid" will be added together with CHORD/GATE).
std::string exportFileName(ExportPart part, bool originalVoicing);

/// Exports the project from the same events that playback uses (generateNotes() for chords on the chord channel,
/// generateMelodyNotes() for the melody on the melody channel). Mutes only affect live playback, never the export.
///   Chords   Format 0, one track "Chords"
///   Melody   Format 0, one track "Melody" (channel 1 when melodyOnChannel1)
///   Combined Format 1: a conductor track (name, time signature, tempo, key) plus tracks "Chords" and "Melody",
///            so tempo and meter are shared; each part also lands on its own MIDI channel.
/// All parts have the same end-of-track (project length, or the bar range), so loops line up when imported.
/// Throws std::invalid_argument for an invalid progression or bar range.
std::vector<std::uint8_t> exportProject(const Progression& progression, const ProjectExportOptions& options = {},
                                        const GenerationOptions& generation = {});

}  // namespace chordcore
