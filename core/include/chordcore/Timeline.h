#pragma once

#include <cstddef>
#include <string>
#include <vector>

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

/// Beat / HalfBeat are relative to the time signature's denominator (an eighth / sixteenth in 6/8). The *Note and
/// *Triplet modes are fixed note values, whatever the time signature: at 960 ticks per quarter a 1/8 note is 480
/// ticks, a 1/16 note 240, an eighth-note triplet 320 and a sixteenth-note triplet 160, so straight and triplet
/// positions are exact integers.
enum class SnapMode {
    Off, Bar, Beat, HalfBeat,
    HalfNote, QuarterNote, EighthNote, SixteenthNote, ThirtySecondNote, EighthTriplet, SixteenthTriplet
};

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
    std::vector<std::size_t> indices;  ///< group operations: the affected blocks (for duplicate: the new copies)
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

/// Inserts a chord at the snapped tick. Its length is `duration` ticks (0 = one bar), shortened to fit the gap
/// before the next block; the duration is independent of the snap unit. Fails if the snapped position is inside an
/// existing block or past the end limit. Keeps blocks sorted by start.
OpResult addBlock(Progression& progression, const ChordSpec& chord, Tick atTick, Tick unit, Tick duration = 0);

/// Moves several blocks together by the same amount, keeping their relative timing. The delta is chosen so that
/// block `anchor` lands on the snapped `desiredAnchorStart`, then clamped so that no moved block overlaps, or passes,
/// a block that is not being moved. `indices` must be unique and valid.
OpResult moveBlocks(Progression& progression, const std::vector<std::size_t>& indices, std::size_t anchor,
                    Tick desiredAnchorStart, Tick unit);

/// Copies the selected blocks as one group, placed right after the group (the copy of the earliest block starts
/// where the group ends), preserving relative timing, durations, chords and octaves. Fails, changing nothing,
/// if the copies would overlap another block or pass the end limit. `indices` of the result are the copies.
OpResult duplicateBlocks(Progression& progression, const std::vector<std::size_t>& indices);

/// Deletes several blocks at once.
OpResult deleteBlocks(Progression& progression, const std::vector<std::size_t>& indices);

/// Cuts a block into two equal-chord blocks at the snapped tick, which must lie strictly inside it.
OpResult splitBlock(Progression& progression, std::size_t index, Tick atTick, Tick unit);

/// Copies a block immediately after itself. Fails if there is no room.
OpResult duplicateBlock(Progression& progression, std::size_t index);

OpResult deleteBlock(Progression& progression, std::size_t index);

/// Moves one chord up or down by `delta` octaves (its own shift, -2..+2). Fails at the limits.
OpResult shiftBlockOctave(Progression& progression, std::size_t index, int delta);

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
