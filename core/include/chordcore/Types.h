#pragma once

#include <cstdint>

namespace chordcore {

/// Musical time is stored as integer ticks so generation and export are exact and deterministic.
using Tick = std::int64_t;

/// Ticks per quarter note. Used internally AND as the Standard MIDI File division.
constexpr int kTicksPerQuarter = 960;

struct TimeSignature {
    int numerator = 4;
    int denominator = 4;   ///< must be a power of two between 1 and 64

    /// numerator 1..32 and denominator one of 1,2,4,8,16,32,64.
    bool isValid() const noexcept;

    /// Length of one bar in ticks (e.g. 3840 for 4/4, 2880 for 3/4 or 6/8).
    Tick ticksPerBar() const noexcept;
};

inline bool operator==(const TimeSignature& a, const TimeSignature& b) noexcept {
    return a.numerator == b.numerator && a.denominator == b.denominator;
}
inline bool operator!=(const TimeSignature& a, const TimeSignature& b) noexcept { return !(a == b); }

}  // namespace chordcore
