#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <chordcore/Progression.h>
#include <chordcore/Timeline.h>

#include <functional>

/// The chord timeline: one coloured card per chord block, laid out to the width of the component.
///
/// This class only draws and turns mouse input into calls to chordcore's timeline operations
/// (Timeline.h, unit-tested). Dragging is previewed locally and committed as ONE edit on mouse-up, so a drag
/// is one undo step and never disturbs playback halfway.
///
///   click a card         select it
///   drag a card          move it (snaps to the grid, stops at its neighbours)
///   drag a card's edge   resize it
///   click a "+" slot     add a tonic chord in that empty bar
class TimelineComponent final : public juce::Component, private juce::Timer {
public:
    explicit TimelineComponent(ChordSketchProcessor&);
    ~TimelineComponent() override = default;

    void setProgression(const chordcore::Progression&);
    void setSelectedIndex(int index);  ///< -1 = none
    int getSelectedIndex() const noexcept { return selectedIndex; }
    void setSnapMode(chordcore::SnapMode mode) { snapMode = mode; }

    std::function<void(int)> onSelectionChanged;  ///< the user selected a block (or -1)
    std::function<void(const juce::String&)> onMessage;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    enum class DragKind { None, Move, ResizeStart, ResizeEnd };

    void timerCallback() override;
    chordcore::TimelineView currentView() const;
    void select(int index);
    void commitDrag();
    void commitAdd(chordcore::Tick atTick);
    chordcore::Tick unitTicks() const;

    ChordSketchProcessor& processor;
    chordcore::Progression model;    // what the document holds
    chordcore::Progression preview;  // what is drawn while dragging
    int selectedIndex = -1;
    chordcore::SnapMode snapMode = chordcore::SnapMode::Bar;

    // Mouse gesture state
    bool gestureActive = false;  // a button is down on a card or an empty slot
    bool dragging = false;       // the pointer has moved far enough to count as a drag
    DragKind dragKind = DragKind::None;
    int dragIndex = -1;
    chordcore::Tick grabOffset = 0;
    double frozenZoom = 0.05;  // pixels per tick, fixed for the duration of a drag
    juce::Point<float> downPosition;
    bool pressedOnSlot = false;
    chordcore::Tick pressedSlotTick = 0;

    double lastPlayheadX = -1.0;
    bool lastPlayheadPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};
