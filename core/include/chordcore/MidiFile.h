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

}  // namespace chordcore
