#pragma once

#include <string>
#include <vector>

#include <chordcore/Progression.h>
#include <chordcore/Timeline.h>
#include <chordcore/Types.h>

namespace chordcore {

// Musical positions, the ruler and zoom/scroll of the chord grid. JUCE-free and unit-tested. Musical time is the
// only authority: pixels exist only inside TimelineView and never flow back into chord positions.

/// Length of one beat in ticks (the time signature's denominator note): 960 in x/4, 480 in x/8.
Tick beatTicks(const TimeSignature& timeSignature);

/// The named sixteenth-note counting inside a beat: 1 e & a.
enum class Subdivision { Beat, E, And, A, Other };

struct MusicalPosition {
    int bar = 1;            ///< 1-based
    int beat = 1;           ///< 1-based, within the bar
    Subdivision sub = Subdivision::Beat;
    Tick ticksIntoBeat = 0;
    Tick beatLength = 960;
};

/// Where a tick falls: bar, beat and subdivision. Negative ticks are treated as 0.
MusicalPosition positionOf(const TimeSignature& timeSignature, Tick tick);

/// The inverse: the tick of "bar B, beat N, e/&/a". In 4/4 at 960 ticks per quarter, the "&" of beat 2 is
/// 1440 and the "a" of beat 3 is 2640. Throws std::invalid_argument for a bar or beat that does not exist or
/// for Subdivision::Other.
Tick tickOf(const TimeSignature& timeSignature, int bar, int beat, Subdivision sub);

/// "Bar 2, beat 2 &", "Bar 1, beat 3 a", "Bar 4, beat 1"; positions between named subdivisions read
/// "Bar 1, beat 2 +120t" (ticks past the beat).
std::string positionLabel(const TimeSignature& timeSignature, Tick tick);

// ---- ruler

enum class MarkLevel { Bar, Beat, Subdivision };

struct RulerMark {
    Tick tick = 0;
    MarkLevel level = MarkLevel::Bar;
    std::string label;       ///< bar number, beat number, or "e" / "&" / "a" (empty when there is none)
    bool showLine = true;    ///< false when marks of this level would be too crowded to draw
    bool showLabel = false;  ///< false when the label would not fit (so labels thin out when zoomed out)
};

/// Grid marks in [firstTick, lastTick] for the ruler and the grid lines. Bars and beats are always the
/// backbone; subdivision marks follow the snap unit (`gridUnit`): 1/8 gives "&", 1/16 gives "e & a", finer or
/// triplet units give unlabelled lines. Crowded levels hide their lines/labels based on `pixelsPerTick`; the
/// musical positions of the marks never depend on zoom.
std::vector<RulerMark> buildRuler(const TimeSignature& timeSignature, Tick firstTick, Tick lastTick, double pixelsPerTick,
                                  Tick gridUnit);

// ---- zoom and scroll

constexpr double kMaxPixelsPerTick = 0.6;  ///< about 576 px per quarter note

/// How far the timeline can scroll: the project length (at least four bars) plus one spare bar.
Tick scrollExtent(const Progression& progression);

/// Zoom that shows the project exactly across `widthPixels`, from the start.
TimelineView fitView(const Progression& progression, double widthPixels);

/// Keeps the view inside [0, extentTicks] and the zoom within its limits.
TimelineView clampView(TimelineView view, Tick extentTicks, double widthPixels);

/// Scales the zoom by `factor` (> 1 zooms in) keeping the tick under `anchorX` where it is, to within a tick.
TimelineView zoomView(const TimelineView& view, double anchorX, double factor, Tick extentTicks, double widthPixels);

/// Scrolls by a number of pixels (positive = later in time).
TimelineView scrollView(const TimelineView& view, double deltaPixels, Tick extentTicks, double widthPixels);

}  // namespace chordcore
