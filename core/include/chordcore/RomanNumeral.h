#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <chordcore/Theory.h>

namespace chordcore {

struct ParseResult {
    bool ok = false;
    ChordSpec chord;
    std::string error;  ///< human-readable reason when ok == false
};

/// Parses a Roman numeral chord symbol into a key-independent ChordSpec.
///
/// Grammar (M1 subset):
///   [b | # | U+266D | U+266F] NUMERAL [o | U+00B0 | U+00F8 | +] [figure]
///   NUMERAL  I..VII (major) or i..vii (minor); one case only
///   o / U+00B0  diminished (lowercase numeral)     U+00F8  half-diminished (lowercase numeral)
///   +           augmented (uppercase numeral)
///   [sus2 | sus4 | sus]  optional suspension, last
///   figure      (none) | 6 | 64 | 7 | 65 | 43 | 42 | 2 | maj7 | M7
///               6 and 64 are triad inversions; 65, 43, 42 (or 2) are seventh-chord inversions.
///
/// Suspensions: append sus2, sus4 or sus (= sus4) after any figure: Vsus4, V7sus4, Isus2, I64sus4.
/// A suspension replaces the third, so it cannot be combined with o, half-diminished or +.
///
/// Not yet supported (later milestones): ninths, alterations, secondary chords.
ParseResult parseRomanNumeral(std::string_view text);

/// The inverse of parseRomanNumeral(): the canonical symbol for a chord, e.g. "V65", "bVII", "vii\xC3\xB8" "7",
/// "I64sus4". Returns std::nullopt for an invalid chord and for chords the grammar cannot express:
///   - a suspension combined with a diminished or augmented quality,
///   - a diminished seventh on a major, minor or augmented chord,
///   - an inverted major-seventh chord.
/// For every other chord, parseRomanNumeral(*formatRomanNumeral(c)).chord == c.
std::optional<std::string> formatRomanNumeral(const ChordSpec& chord);

}  // namespace chordcore
