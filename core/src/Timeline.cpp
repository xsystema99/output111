#include <chordcore/Timeline.h>

#include <algorithm>
#include <cmath>

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
    if (!ts.isValid()) return 1;
    const Tick beat = static_cast<Tick>(kTicksPerQuarter) * 4 / ts.denominator;
    switch (mode) {
        case SnapMode::Off: return 1;
        case SnapMode::Bar: return ts.ticksPerBar();
        case SnapMode::Beat: return std::max<Tick>(beat, 1);
        case SnapMode::HalfBeat: return std::max<Tick>(beat / 2, 1);
    }
    return 1;
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

OpResult addBlock(Progression& p, const ChordSpec& chord, Tick atTick, Tick unit) {
    if (!chord.isValid()) return fail("invalid chord");
    if (!p.timeSignature.isValid()) return fail("invalid time signature");
    const Tick start = std::max<Tick>(snapToUnit(atTick, unit), 0);
    if (start >= maxEndTick(p.timeSignature)) return fail("beyond the end of the timeline");
    if (findBlockAt(p, start) >= 0) return fail("there is already a chord there");

    const Tick room = nextLimit(p, p.blocks.size(), start) - start;  // self index out of range: nobody ignored
    if (room < 1) return fail("no room for a chord there");

    ChordBlock added{chord, start, std::min(p.timeSignature.ticksPerBar(), room)};
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
    ChordBlock second{block.chord, at, block.endTick() - at};
    p.blocks.insert(p.blocks.begin() + static_cast<std::ptrdiff_t>(index) + 1, second);
    return succeed(index + 1);
}

OpResult duplicateBlock(Progression& p, std::size_t index) {
    if (index >= p.blocks.size()) return fail("no such block");
    const ChordBlock block = p.blocks[index];
    const Tick start = block.endTick();
    const Tick room = nextLimit(p, index, block.startTick) - start;
    if (room < block.durationTicks) return fail("no room after the chord");

    p.blocks.insert(p.blocks.begin() + static_cast<std::ptrdiff_t>(index) + 1,
                    ChordBlock{block.chord, start, block.durationTicks});
    return succeed(index + 1);
}

OpResult deleteBlock(Progression& p, std::size_t index) {
    if (index >= p.blocks.size()) return fail("no such block");
    p.blocks.erase(p.blocks.begin() + static_cast<std::ptrdiff_t>(index));
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
