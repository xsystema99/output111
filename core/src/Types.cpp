#include <chordcore/Types.h>

namespace chordcore {

bool TimeSignature::isValid() const noexcept {
    if (numerator < 1 || numerator > 32) return false;
    if (denominator < 1 || denominator > 64) return false;
    return (denominator & (denominator - 1)) == 0;  // power of two
}

Tick TimeSignature::ticksPerBar() const noexcept {
    // 960 * 4 = 3840 is divisible by every permitted denominator, so this is exact.
    return static_cast<Tick>(kTicksPerQuarter) * 4 * numerator / denominator;
}

}  // namespace chordcore
