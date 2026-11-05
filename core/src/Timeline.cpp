#include <chordcore/Timeline.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace chordcore {

namespace {

constexpr int kMaxBars = 256;

OpResult fail(std::string message) {
    OpResult r;
    r.error = std::move(message);
    return r;
}

OpResult succeed(std::size_t index) {
    OpResult r;
    r.ok = true;
    r.index = index;
    return r;
}

Tick floorDiv(Tick a, Tick b) {
    Tick q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

/// End of the closest block that finishes at or before `start` (0 if none), ignoring `self`.
Tick previousLimit(const Progression& p, std::size_t self, Tick start) {
    Tick limit = 0;
    for (std::size_t i = 0; i < p.blocks.size(); ++i) {
        if (i != self && p.blocks[i].startTick < start) limit = std::max(limit, p.blocks[i].endTick());
    }
    return limit;
}

/// Start of the closest block that begins at or after `start`, or the global end limit, ignoring `self`.
Tick nextLimit(const Progression& p, std::size_t self, Tick start) {
    Tick limit = maxEndTick(p.timeSignature);
    for (std::size_t i = 0; i < p.blocks.size(); ++i) {
        if (i != self && p.blocks[i].startTick >= start) limit = std::min(limit, p.blocks[i].startTick);
    }
    return limit;
}

}  // namespace

Tick snapUnitTicks(SnapMode mode, const TimeSignature& ts) {
    constexpr Tick quarter = kTicksPerQuarter;
    switch (mode) {  // fixed note values do not depend on the time signature
        case SnapMode::Off: return 1;
        case SnapMode::HalfNote: return quarter * 2;
        case SnapMode::QuarterNote: return quarter;
        case SnapMode::EighthNote: return quarter / 2;
        case SnapMode::SixteenthNote: return quarter / 4;
        case SnapMode::ThirtySecondNote: return quarter / 8;
        case SnapMode::EighthTriplet: return quarter / 3;
        case SnapMode::SixteenthTriplet: return quarter / 6;
        case SnapMode::Bar:
        case SnapMode::Beat:
        case SnapMode::HalfBeat: break;
    }
    if (!ts.isValid()) return 1;
    const Tick beat = quarter * 4 / ts.denominator;
    switch (mode) {
        case SnapMode::Bar: return ts.ticksPerBar();
        case SnapMode::Beat: return std::max<Tick>(beat, 1);
        case SnapMode::HalfBeat: return std::max<Tick>(beat / 2, 1);
        default: return 1;
    }
}

Tick snapToUnit(Tick tick, Tick unit) {
    if (unit < 1) unit = 1;
    return floorDiv(tick + unit / 2, unit) * unit;
}

Tick maxEndTick(const TimeSignature& ts) {
    return (ts.isValid() ? ts.ticksPerBar() : 3840) * kMaxBars;
}

int findBlockAt(const Progression& p, Tick tick) {
    for (std::size_t i = 0; i < p.blocks.size(); ++i) {
        if (tick >= p.blocks[i].startTick && tick < p.blocks[i].endTick()) return static_cast<int>(i);
    }
    return -1;
}

OpResult moveBlock(Progression& p, std::size_t index, Tick desiredStart, Tick unit) {
    if (index >= p.blocks.size()) return fail("no such block");
    ChordBlock& block = p.blocks[index];
    const Tick lo = previousLimit(p, index, block.startTick);
    const Tick hi = nextLimit(p, index, block.startTick) - block.durationTicks;
    // lo <= current start <= hi holds for a valid progression, so the clamp range is never inverted.
    block.startTick = std::clamp(snapToUnit(desiredStart, unit), lo, std::max(lo, hi));
    return succeed(index);
}

OpResult resizeBlockStart(Progression& p, std::size_t index, Tick desiredStart, Tick unit) {
    if (index >= p.blocks.size()) return fail("no such block");
    ChordBlock& block = p.blocks[index];
    const Tick end = block.endTick();
    const Tick minDuration = std::max<Tick>(unit, 1);
    const Tick lo = previousLimit(p, index, block.startTick);
    const Tick hi = std::max(block.startTick, end - minDuration);  // never forced to grow
    block.startTick = std::clamp(snapToUnit(desiredStart, unit), lo, hi);
    block.durationTicks = end - block.startTick;
    return succeed(index);
}

OpResult resizeBlockEnd(Progression& p, std::size_t index, Tick desiredEnd, Tick unit) {
    if (index >= p.blocks.size()) return fail("no such block");
    ChordBlock& block = p.blocks[index];
    const Tick minDuration = std::max<Tick>(unit, 1);
    const Tick lo = std::min(block.startTick + minDuration, block.endTick());  // never forced to grow
    const Tick hi = std::max(nextLimit(p, index, block.startTick), block.endTick());
    const Tick end = std::clamp(snapToUnit(desiredEnd, unit), lo, hi);
    block.durationTicks = end - block.startTick;
    return succeed(index);
}

OpResult addBlock(Progression& p, const ChordSpec& chord, Tick atTick, Tick unit, Tick duration) {
    if (!chord.isValid()) return fail("invalid chord");
    if (!p.timeSignature.isValid()) return fail("invalid time signature");
    const Tick start = std::max<Tick>(snapToUnit(atTick, unit), 0);
    if (start >= maxEndTick(p.timeSignature)) return fail("beyond the end of the timeline");
    if (findBlockAt(p, start) >= 0) return fail("there is already a chord there");

    const Tick room = nextLimit(p, p.blocks.size(), start) - start;  // self index out of range: nobody ignored
    if (room < 1) return fail("no room for a chord there");

    const Tick wanted = duration > 0 ? duration : p.timeSignature.ticksPerBar();
    ChordBlock added{chord, start, std::min(wanted, room)};
    std::size_t position = 0;
    while (position < p.blocks.size() && p.blocks[position].startTick < start) ++position;
    p.blocks.insert(p.blocks.begin() + static_cast<std::ptrdiff_t>(position), added);
    return succeed(position);
}

OpResult splitBlock(Progression& p, std::size_t index, Tick atTick, Tick unit) {
    if (index >= p.blocks.size()) return fail("no such block");
    const ChordBlock block = p.blocks[index];
    const Tick at = snapToUnit(atTick, unit);
    if (at <= block.startTick || at >= block.endTick()) return fail("the split point must be inside the chord");

    p.blocks[index].durationTicks = at - block.startTick;
    ChordBlock second = block;  // keeps the chord's octave
    second.startTick = at;
    second.durationTicks = block.endTick() - at;
    p.blocks.insert(p.blocks.begin() + static_cast<std::ptrdiff_t>(index) + 1, second);
    return succeed(index + 1);
}

OpResult duplicateBlock(Progression& p, std::size_t index) {
    if (index >= p.blocks.size()) return fail("no such block");
    const ChordBlock block = p.blocks[index];
    const Tick start = block.endTick();
    const Tick room = nextLimit(p, index, block.startTick) - start;
    if (room < block.durationTicks) return fail("no room after the chord");

    ChordBlock copy = block;  // keeps the chord's octave
    copy.startTick = start;
    p.blocks.insert(p.blocks.begin() + static_cast<std::ptrdiff_t>(index) + 1, copy);
    return succeed(index + 1);
}

namespace {

/// Unique, valid, ascending copy of `indices`; false if any index is invalid or repeated.
bool normalised(const Progression& p, const std::vector<std::size_t>& indices, std::vector<std::size_t>& out) {
    out = indices;
    std::sort(out.begin(), out.end());
    if (out.empty() || std::adjacent_find(out.begin(), out.end()) != out.end()) return false;
    return out.back() < p.blocks.size();
}

}  // namespace

OpResult moveBlocks(Progression& p, const std::vector<std::size_t>& indices, std::size_t anchor, Tick desiredAnchorStart, Tick unit) {
    std::vector<std::size_t> sel;
    if (!normalised(p, indices, sel)) return fail("no such block");
    if (std::find(sel.begin(), sel.end(), anchor) == sel.end()) return fail("the anchor must be one of the moved blocks");

    std::vector<char> moving(p.blocks.size(), 0);
    for (auto i : sel) moving[i] = 1;

    Tick deltaMin = std::numeric_limits<Tick>::min() / 4, deltaMax = std::numeric_limits<Tick>::max() / 4;
    const Tick endLimit = maxEndTick(p.timeSignature);
    for (auto i : sel) {
        const ChordBlock& b = p.blocks[i];
        Tick before = 0, after = endLimit;  // nearest non-moving neighbours
        for (std::size_t j = 0; j < p.blocks.size(); ++j) {
            if (moving[j]) continue;
            if (p.blocks[j].startTick < b.startTick) before = std::max(before, p.blocks[j].endTick());
            else after = std::min(after, p.blocks[j].startTick);
        }
        deltaMin = std::max(deltaMin, before - b.startTick);
        deltaMax = std::min(deltaMax, std::max(after, b.endTick()) - b.endTick());
    }
    deltaMin = std::min<Tick>(deltaMin, 0);
    deltaMax = std::max<Tick>(deltaMax, 0);

    Tick delta = snapToUnit(desiredAnchorStart, unit) - p.blocks[anchor].startTick;
    delta = std::clamp(delta, deltaMin, deltaMax);
    for (auto i : sel) p.blocks[i].startTick += delta;

    OpResult r = succeed(anchor);
    r.indices = sel;
    return r;
}

OpResult duplicateBlocks(Progression& p, const std::vector<std::size_t>& indices) {
    std::vector<std::size_t> sel;
    if (!normalised(p, indices, sel)) return fail("no such block");

    Tick first = p.blocks[sel.front()].startTick, last = p.blocks[sel.front()].endTick();
    for (auto i : sel) {
        first = std::min(first, p.blocks[i].startTick);
        last = std::max(last, p.blocks[i].endTick());
    }
    const Tick offset = last - first;

    std::vector<ChordBlock> copies;
    for (auto i : sel) {
        ChordBlock c = p.blocks[i];  // chord, duration and octave are preserved
        c.startTick += offset;
        copies.push_back(c);
    }

    const Tick endLimit = maxEndTick(p.timeSignature);
    for (const auto& c : copies) {
        if (c.endTick() > endLimit) return fail("no room: the copy would go past the end of the timeline");
        for (const auto& existing : p.blocks) {
            if (c.startTick < existing.endTick() && existing.startTick < c.endTick()) return fail("no room: the copy would overlap another chord");
        }
    }

    for (const auto& c : copies) p.blocks.push_back(c);
    std::stable_sort(p.blocks.begin(), p.blocks.end(), [](const ChordBlock& a, const ChordBlock& b) { return a.startTick < b.startTick; });

    OpResult r = succeed(0);
    for (const auto& c : copies) {
        for (std::size_t i = 0; i < p.blocks.size(); ++i) {
            if (p.blocks[i].startTick == c.startTick) { r.indices.push_back(i); break; }
        }
    }
    if (!r.indices.empty()) r.index = r.indices.front();
    return r;
}

OpResult deleteBlocks(Progression& p, const std::vector<std::size_t>& indices) {
    std::vector<std::size_t> sel;
    if (!normalised(p, indices, sel)) return fail("no such block");
    for (auto it = sel.rbegin(); it != sel.rend(); ++it) p.blocks.erase(p.blocks.begin() + static_cast<std::ptrdiff_t>(*it));
    OpResult r = succeed(sel.front());
    r.indices = sel;
    return r;
}

OpResult deleteBlock(Progression& p, std::size_t index) {
    if (index >= p.blocks.size()) return fail("no such block");
    p.blocks.erase(p.blocks.begin() + static_cast<std::ptrdiff_t>(index));
    return succeed(index);
}

OpResult shiftBlockOctave(Progression& p, std::size_t index, int delta) {
    if (index >= p.blocks.size()) return fail("no such block");
    const int target = p.blocks[index].octave + delta;
    if (target < -kMaxOctaveShift) return fail("already at the lowest octave");
    if (target > kMaxOctaveShift) return fail("already at the highest octave");
    p.blocks[index].octave = target;
    return succeed(index);
}

OpResult setBlockChord(Progression& p, std::size_t index, const ChordSpec& chord) {
    if (index >= p.blocks.size()) return fail("no such block");
    if (!chord.isValid()) return fail("invalid chord");
    p.blocks[index].chord = chord;
    return succeed(index);
}

double TimelineView::tickToX(Tick tick) const noexcept {
    return static_cast<double>(tick - originTick) * pixelsPerTick;
}

Tick TimelineView::xToTick(double x) const noexcept {
    if (!(pixelsPerTick > 0.0) || !std::isfinite(x)) return originTick;
    return originTick + static_cast<Tick>(std::llround(x / pixelsPerTick));
}

double fitPixelsPerTick(const Progression& p, double widthPixels) {
    const Tick bar = p.timeSignature.isValid() ? p.timeSignature.ticksPerBar() : 3840;
    const Tick span = std::max(p.lengthTicks(), bar * 4);  // whole bars, at least four
    return std::max(widthPixels, 1.0) / static_cast<double>(span);
}

Hit hitTest(const Progression& p, const TimelineView& view, double x, double edgeWidthPixels) {
    for (std::size_t i = 0; i < p.blocks.size(); ++i) {
        const double left = view.tickToX(p.blocks[i].startTick);
        const double right = view.tickToX(p.blocks[i].endTick());
        if (x < left || x >= right) continue;

        const double edge = std::min(std::max(edgeWidthPixels, 0.0), (right - left) / 3.0);
        Hit hit;
        hit.index = static_cast<int>(i);
        hit.zone = x < left + edge ? HitZone::LeftEdge : (x >= right - edge ? HitZone::RightEdge : HitZone::Body);
        return hit;
    }
    return {};
}

}  // namespace chordcore
