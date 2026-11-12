#include <chordcore/ScaleLimiter.h>

#include <algorithm>
#include <cmath>

#include <chordcore/Harmony.h>
#include <chordcore/MelodyEdit.h>

namespace chordcore {

namespace {

int mod12(int v) { return ((v % 12) + 12) % 12; }

}  // namespace

bool pitchInScale(const Key& key, int pitch) {
    return ((scalePitchClassMask(key) >> mod12(pitch)) & 1u) != 0;
}

std::vector<int> visibleRowPitches(const Key& key, bool limiterOn, int low, int high) {
    std::vector<int> rows;
    low = std::max(low, 0);
    high = std::min(high, 127);
    if (high < low) return rows;
    rows.reserve(static_cast<std::size_t>(high - low + 1));
    const auto mask = scalePitchClassMask(key);
    for (int pitch = low; pitch <= high; ++pitch) {
        if (!limiterOn || ((mask >> mod12(pitch)) & 1u) != 0) rows.push_back(pitch);
    }
    return rows;
}

int scaleDegreeCount(const Key& key) {
    int n = 0;
    for (auto mask = scalePitchClassMask(key); mask != 0; mask = static_cast<std::uint16_t>(mask >> 1)) n += mask & 1u;
    return n;
}

int stepScalePitch(const Key& key, int pitch, int steps) {
    pitch = std::clamp(pitch, 0, 127);
    if (steps == 0) return pitch;
    const auto rows = visibleRowPitches(key, true);
    if (rows.empty()) return pitch;
    // First scale pitch at or above `pitch`.
    const auto it = std::lower_bound(rows.begin(), rows.end(), pitch);
    std::ptrdiff_t index = it - rows.begin();  // not `long`: 32-bit on Windows
    const bool exact = it != rows.end() && *it == pitch;
    std::ptrdiff_t target;
    if (exact) target = index + steps;
    else target = steps > 0 ? index + steps - 1 : index + steps;  // between two rows: the first step reaches the neighbour
    target = std::clamp<std::ptrdiff_t>(target, 0, static_cast<std::ptrdiff_t>(rows.size()) - 1);
    return rows[static_cast<std::size_t>(target)];
}

int pitchForScaleDegree(const Key& key, int degree, int referencePitch, int low, int high) {
    const int count = scaleDegreeCount(key);
    if (degree < 1 || degree > count) return -1;
    // The scale's pitch classes in order starting at the tonic.
    const auto mask = scalePitchClassMask(key);
    int found = 0, pc = -1;
    for (int offset = 0; offset < 12; ++offset) {
        const int candidate = mod12(key.tonic + offset);
        if (((mask >> candidate) & 1u) != 0 && ++found == degree) { pc = candidate; break; }
    }
    if (pc < 0) return -1;
    low = std::max(low, 0);
    high = std::min(high, 127);
    int best = -1, bestDistance = 1 << 20;
    for (int pitch = low; pitch <= high; ++pitch) {
        if (mod12(pitch) != pc) continue;
        const int d = std::abs(pitch - referencePitch);
        if (d < bestDistance) { best = pitch; bestDistance = d; }  // ties: the lower octave (first found)
    }
    return best;
}

int outOfScaleNoteCount(const Progression& progression) {
    int n = 0;
    for (const auto& note : progression.melody) n += pitchInScale(progression.key, note.pitch) ? 0 : 1;
    return n;
}

std::vector<std::size_t> editableNoteIndices(const Progression& progression) {
    std::vector<std::size_t> out;
    out.reserve(progression.melody.size());
    for (std::size_t i = 0; i < progression.melody.size(); ++i) {
        if (!progression.melodySettings.scaleLimiter || pitchInScale(progression.key, progression.melody[i].pitch)) out.push_back(i);
    }
    return out;
}

int chromaticChordToneCount(const Key& key, const ChordSpec& chord) {
    auto outside = static_cast<std::uint16_t>(chordPitchClassMask(key, chord) & ~scalePitchClassMask(key));
    int n = 0;
    for (; outside != 0; outside = static_cast<std::uint16_t>(outside >> 1)) n += outside & 1u;
    return n;
}

OpResult moveMelodyNotesByDegrees(Progression& p, const std::vector<std::size_t>& indices, std::size_t anchor,
                                  Tick desiredAnchorStart, int degreeDelta, Tick unit) {
    OpResult bad;
    if (indices.empty()) { bad.error = "invalid selection"; return bad; }
    for (auto i : indices) {
        if (i >= p.melody.size()) { bad.error = "invalid selection"; return bad; }
        if (!pitchInScale(p.key, p.melody[i].pitch)) { bad.error = "a selected note is outside the scale: turn off Scale Limiter to edit it"; return bad; }
    }
    // Clamp the degree shift so the whole group stays on scale rows inside 0..127.
    const auto rows = visibleRowPitches(p.key, true);
    int lowestRow = static_cast<int>(rows.size()), highestRow = 0;
    for (auto i : indices) {
        const int row = static_cast<int>(std::lower_bound(rows.begin(), rows.end(), p.melody[i].pitch) - rows.begin());
        lowestRow = std::min(lowestRow, row);
        highestRow = std::max(highestRow, row);
    }
    const int dd = std::clamp(degreeDelta, -lowestRow, static_cast<int>(rows.size()) - 1 - highestRow);

    // Re-pitch the notes on a scratch copy (same order, so the indices stay valid), then let the ordinary move do the
    // time shift, clamping, overlap check and re-sort with a zero semitone shift. Nothing changes unless it succeeds.
    Progression trial = p;
    for (auto i : indices) {
        const int row = static_cast<int>(std::lower_bound(rows.begin(), rows.end(), trial.melody[i].pitch) - rows.begin());
        trial.melody[i].pitch = rows[static_cast<std::size_t>(row + dd)];
    }
    const auto r = moveMelodyNotes(trial, indices, anchor, desiredAnchorStart, 0, unit);
    if (!r.ok) return r;
    p = std::move(trial);
    return r;
}

}  // namespace chordcore
