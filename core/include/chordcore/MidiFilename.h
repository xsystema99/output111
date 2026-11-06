#pragma once

#include <string>
#include <vector>

#include <chordcore/MidiFile.h>
#include <chordcore/MidiFilenameFormat.h>
#include <chordcore/Progression.h>

namespace chordcore {

/// One chord as it appears in a name: "Dbmaj7" / "bVImaj7" / "Dbmaj7 (bVImaj7)" with display glyphs
/// (flat sign, sharp sign, degree sign), or plain ASCII when `ascii` is set. Roman numerals are the chromatic ones relative
/// to the project tonic (chromaticRomanNumeral); a chord with no Roman symbol shows its absolute name in Roman mode.
std::string chordLabel(const Key& key, const ChordSpec& chord, MidiFilenameFormat format, bool ascii = false);

/// The chord events of an export region in musical order: sorted by start tick, ties keep their stored order, repeated
/// chords are all kept. firstBar/lastBar are 1-based and inclusive, 0/0 = the whole project; a chord belongs to the region
/// when any of it lies inside, the same rule the MIDI clip uses.
std::vector<std::size_t> chordsInRegion(const Progression& progression, int firstBar = 0, int lastBar = 0);

/// All chords of the region joined with " - " (en dash in display mode), e.g. "Fm9 (i9) - Dbmaj7 (bVImaj7)".
std::string progressionLabel(const Progression& progression, MidiFilenameFormat format, int firstBar = 0, int lastBar = 0, bool ascii = false);

/// The one filename generator: used by the live preview, "Export MIDI...", and the drag handle. UTF-8, ends in ".mid".
///   Chords / Combined   the progression label of the region, then "_voiced" when the chords are exported as played
///                       (original voicing = no suffix), then "_combined" for the Combined file. No gated export exists yet.
///   Melody              the existing "Melody.mid".
/// Characters Windows forbids become "_", the stem is at most kMaxFilenameUnits long including the suffix and ".mid"
/// (counted in UTF-16 units): longer progressions are cut at a chord boundary and end "+N more [hash]" with a
/// deterministic 6-digit hash of the full name. Never cuts inside a Unicode character. `ascii` is the documented fallback
/// for destinations that cannot take the flat/sharp/degree signs and the en dash.
std::string generateMidiFilename(const Progression& progression, MidiFilenameFormat format, const ProjectExportOptions& options,
                                 bool ascii = false);

constexpr int kMaxFilenameUnits = 150;

/// SMF text metadata written into every export regardless of the filename format (ASCII, FF 01 text events at tick 0):
/// "Absolute Progression: ...", "Roman Progression: ...", "Project Key: F minor". Empty when the region has no chords.
std::vector<std::string> midiMetadataText(const Progression& progression, int firstBar = 0, int lastBar = 0);

const char* toString(MidiFilenameFormat format);

}  // namespace chordcore
