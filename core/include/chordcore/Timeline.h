#pragma once

#include <cstddef>
#include <string>

#include <chordcore/Progression.h>
#include <chordcore/Theory.h>
#include <chordcore/Types.h>

namespace chordcore {

// Everything the timeline editor does to the model lives here, JUCE-free, so it is unit-tested. The JUCE
// component only turns mouse events into calls to these functions and draws the result.
//
// Rules shared by every operation:
//  - Single lane: blocks never overlap and never start before tick 0 or end after maxEndTick().
//  - Dragging is clamped, never rejected: a block stops at its neighbour instead of jumping over it.
//  - An operation that cannot be done (no room, bad index, ...) returns ok == false and leaves the
//    progression untouched. A successful one leaves a valid progression (given a valid input).
//  - `unit` is the snap grid in ticks (1 = free). Edited positions snap to multiples of the unit; a
//    neighbour that sits off-grid wins over the grid, so snapping never causes an overlap.

enum class SnapMode { Off, Bar, Beat, HalfBeat };

/// Grid size in ticks. Off -> 1. Beat is one note of the time signature's denominator (a quarter in 4/4,
/// an eighth in 6/8); HalfBeat is half of that (at least 1 tick).
Tick snapUnitTicks(SnapMode mode, const TimeSignature& timeSignature);

/// Nearest multiple of `unit` (ties round up). unit < 1 is treated as 1. Works for negative values too.
Tick snapToUnit(Tick tick, Tick unit);

/// Furthest tick the editor will place anything at: 256 bars.
Tick maxEndTick(const TimeSignature& timeSignature);

struct OpResult {
    bool ok = false;
    std::string error;
    std::size_t index = 0;  ///< the affected block (for add/split: the new/second block); meaningless after delete
};

/// Index of the block that covers `tick` (start <= tick < end), or -1.
int findBlockAt(const Progression& progression, Tick tick);

/// Moves a block, keeping its length. The start snaps to the grid and is clamped between its neighbours.
OpResult moveBlock(Progression& progression, std::size_t index, Tick desiredStart, Tick unit);

/// Moves the left edge (the end stays). A block never shrinks below one grid unit through this call, and a
/// block already shorter than that is never forced to grow.
OpResult resizeBlockStart(Progression& progression, std::size_t index, Tick desiredStart, Tick unit);

/// Moves the right edge (the start stays); same limits as resizeBlockStart, bounded above by the next block.
OpResult resizeBlockEnd(Progression& progression, std::size_t index, Tick desiredEnd, Tick unit);

/// Inserts a chord at the snapped tick. Its length is one bar, shortened to fit the gap before the next block.
/// Fails if the snapped position is inside an existing block or past the end limit. Keeps blocks sorted by start.
OpResult addBlock(Progression& progression, const ChordSpec& chord, Tick atTick, Tick unit);

/// Cuts a block into two equal-chord blocks at the snapped tick, which must lie strictly inside it.
OpResult splitBlock(Progression& progression, std::size_t index, Tick atTick, Tick unit);

/// Copies a block immediately after itself. Fails if there is no room.
OpResult duplicateBlock(Progression& progression, std::size_t index);

OpResult deleteBlock(Progression& progression, std::size_t index);

/// Replaces the chord of a block (position and length unchanged). Fails for an invalid chord.
OpResult setBlockChord(Progression& progression, std::size_t index, const ChordSpec& chord);

// ---- view geometry (pixels <-> ticks) and hit testing

struct TimelineView {
    double pixelsPerTick = 0.05;
    Tick originTick = 0;  ///< the tick at x == 0

    double tickToX(Tick tick) const noexcept;
    /// Nearest tick to a pixel position (may be negative left of the origin).
    Tick xToTick(double x) const noexcept;
};

/// Zoom that makes the progression (rounded up to whole bars, at least four bars) exactly fill `widthPixels`.
double fitPixelsPerTick(const Progression& progression, double widthPixels);

enum class HitZone { None, Body, LeftEdge, RightEdge };

struct Hit {
    int index = -1;
    HitZone zone = HitZone::None;
};

/// Which block, and which part of it, lies under pixel x. Edge zones are `edgeWidthPixels` wide but never
/// more than a third of the block, so a narrow block keeps a grabbable body.
Hit hitTest(const Progression& progression, const TimelineView& view, double x, double edgeWidthPixels);

}  // namespace chordcore
