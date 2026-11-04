#pragma once

#include <string>
#include <string_view>

#include <chordcore/Progression.h>

namespace chordcore {

/// Version written by serializeProgression(). A reader refuses files with a higher version rather
/// than guessing; unknown extra fields in a same-version file are ignored (forward compatibility).
constexpr int kStateVersion = 1;

/// Serialises a progression to compact, deterministic JSON (UTF-8). Enums are written as readable
/// strings so the format survives reordering of the C++ enums. Tempo is written as the shortest decimal
/// that reads back to the identical double, so a round trip is exact.
///
///   {"format":"chordsketch-state","version":1,"key":{"tonic":0,"mode":"major"},"tempoBpm":120,
///    "timeSignature":{"numerator":4,"denominator":4},"blocks":[{"degree":1,"accidental":0,
///    "quality":"major","seventh":"none","inversion":0,"suspension":"none","startTick":0,
///    "durationTicks":3840}, ...]}
///
/// Throws std::invalid_argument if the progression is invalid (it could not be read back).
std::string serializeProgression(const Progression& progression);

struct DeserializeResult {
    bool ok = false;
    Progression progression;
    std::string error;  ///< human-readable reason when ok == false
};

/// Parses what serializeProgression() wrote. Never throws and never crashes on malformed input
/// (truncated, wrong types, absurd nesting, trailing garbage...). The returned progression has passed
/// Progression::validate().
DeserializeResult deserializeProgression(std::string_view text);

}  // namespace chordcore
