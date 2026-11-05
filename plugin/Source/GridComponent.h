#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <chordcore/ChordEdit.h>
#include <chordcore/Grid.h>
#include <chordcore/Progression.h>
#include <chordcore/Timeline.h>

#include <functional>
#include <vector>

/// The precision chord grid: a scrolling ruler with bar / beat / subdivision lines above one chord lane.
///
/// This class only draws and turns input into calls to chordcore (Grid.h, Timeline.h: unit-tested). Musical
/// time (ticks, 960 per quarter) is the only authority; pixels exist only in the view (origin + zoom) and are
/// converted back to ticks on every event, so zoom and scrolling can never move a chord. Drags are previewed on
/// a local copy of the progression and committed as ONE edit on mouse-up (one undo step).
///
///   click empty lane        insert the armed chord at the grid cell, with the "insert length"
///   drag empty lane         rectangle select (Shift adds)
///   click / Shift-click     select / toggle
///   drag a chord            move it (the whole selection moves together; stops at unselected neighbours)
///   drag a chord's edge     resize
///   Ctrl+D, Delete, Esc     duplicate, delete, clear selection;  Ctrl+A selects all
///   Ctrl+wheel              zoom about the cursor;  wheel / Shift+wheel scroll
class GridComponent final : public juce::Component, private juce::Timer, private juce::ScrollBar::Listener {
public:
    explicit GridComponent(ChordSketchProcessor&);
    ~GridComponent() override;

    void setProgression(const chordcore::Progression&);

    /// Selection by block index (ascending). The primary block is the one the editor panel describes.
    void setSelection(std::vector<std::size_t> indices, int primary);
    const std::vector<std::size_t>& getSelection() const noexcept { return selection; }
    int getPrimary() const noexcept { return primary; }

    void setSnapMode(chordcore::SnapMode mode);
    void setInsertChord(const chordcore::ChordSpec& chord) { insertChord = chord; }
    void setInsertDuration(chordcore::Tick ticks) { insertDuration = ticks; }
    void fitToWindow();

    /// The shared time view (the melody piano roll draws with exactly this origin and zoom) and snap unit.
    const chordcore::TimelineView& getView() const noexcept { return currentView; }
    chordcore::Tick getSnapUnit() const { return unit(); }
    std::function<void()> onViewChanged;  ///< zoom, scroll or project length changed

    void deleteSelection();
    void duplicateSelection();

    std::function<void(const std::vector<std::size_t>&, int)> onSelectionChanged;  ///< indices, primary (-1 none)
    std::function<void(const juce::String&)> onMessage;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    enum class Gesture { None, Move, ResizeStart, ResizeEnd, Marquee, InsertClick };

    void timerCallback() override;
    void scrollBarMoved(juce::ScrollBar*, double newRangeStart) override;

    chordcore::TimelineView view() const { return currentView; }
    double laneWidth() const { return static_cast<double>(getWidth()); }
    chordcore::Tick extent() const { return chordcore::scrollExtent(model); }
    chordcore::Tick unit() const { return chordcore::snapUnitTicks(snapMode, model.timeSignature); }
    chordcore::Tick cellStart(chordcore::Tick tick) const;  ///< the grid cell containing a tick (insertion point)
    juce::Rectangle<int> laneBounds() const;
    juce::Rectangle<int> rulerBounds() const;
    juce::Rectangle<float> blockBounds(const chordcore::ChordBlock&) const;

    void applyView(chordcore::TimelineView next, bool userAction);
    void updateScrollBar();
    void notifyView() { if (onViewChanged) onViewChanged(); }
    void setSelectionInternal(std::vector<std::size_t> indices, int newPrimary, bool notify);
    bool isSelected(std::size_t index) const;
    void commitGesture();
    void commitInsert(chordcore::Tick atTick);
    void report(const juce::String& text) const { if (onMessage) onMessage(text); }

    void drawRuler(juce::Graphics&) const;
    void drawLane(juce::Graphics&) const;
    void drawBlock(juce::Graphics&, const chordcore::ChordBlock&, bool selected, float alpha) const;

    ChordSketchProcessor& chordProcessor;
    chordcore::Progression model;    // what the document holds
    chordcore::Progression preview;  // what is drawn while a drag is in progress
    std::vector<std::size_t> selection;
    int primary = -1;

    chordcore::SnapMode snapMode = chordcore::SnapMode::EighthNote;
    chordcore::ChordSpec insertChord = chordcore::diatonicChord(chordcore::Mode::Major, 1);
    chordcore::Tick insertDuration = chordcore::kTicksPerQuarter;

    chordcore::TimelineView currentView;
    bool userZoomed = false;  // false: the view follows "fit to window"

    // gesture state
    Gesture gesture = Gesture::None;
    bool dragging = false;
    int pressedBlock = -1;
    bool pressedWasSelected = false;
    chordcore::Tick grabOffset = 0;
    chordcore::Tick pressedCell = 0;
    juce::Point<float> downPosition, dragPosition;
    juce::Rectangle<float> marquee;
    std::vector<std::size_t> selectionAtMarqueeStart;

    juce::Point<float> hoverPosition{-1.0f, -1.0f};
    double lastPlayheadX = -1.0;
    bool lastPlayheadPlaying = false;
    juce::String lastPlayheadText;

    juce::ScrollBar scrollBar{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GridComponent)
};
