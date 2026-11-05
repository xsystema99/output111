#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <chordcore/Progression.h>
#include <chordcore/Types.h>

namespace chordcore {

struct ParseProgressionResult {
    bool ok = false;
    std::vector<ChordBlock> blocks;
    std::string error;  ///< human-readable reason when ok == false
};

/// Parses a one-line chord progression such as  "I V vi:2 - IV7:0.5 Vsus4"
///
///   token       Roman numeral chord (see parseRomanNumeral) or "-" for a rest
///   @+N / @-N   the chord's own octave shift, -2..+2 (default 0), after the length: "V9:2@+1"
///   :N          length in bars (default 1): integers or decimals such as 0.5; must be > 0 and <= 64
///   separators  whitespace, ',' or '|'
///
/// Chords are laid end to end from tick 0; a rest only advances the position. Trailing rests produce no
/// block. An empty string is a valid, empty progression. At most 256 tokens.
ParseProgressionResult parseProgressionText(std::string_view text, const TimeSignature& timeSignature);

/// The inverse: one line per progression, e.g. "I V vi:2 - IV". Gaps between blocks become rests. A chord
/// the symbol grammar cannot express is written as "?" (such a line does not parse back).
/// parseProgressionText(formatProgressionText(p)) reproduces p's blocks (chords, inversions, 9ths, octaves,
/// lengths and gaps) whenever every chord is expressible.
std::string formatProgressionText(const Progression& progression);

}  // namespace chordcore
