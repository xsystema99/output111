#pragma once

#include "GridComponent.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <chordcore/Harmony.h>
#include <chordcore/Melody.h>
#include <chordcore/MelodyEdit.h>
#include <chordcore/Progression.h>

#include <array>
#include <functional>
#include <vector>

/// The melody piano roll, directly under the chord grid. It draws with the chord grid's own view (same origin, zoom,
/// width, snap and beat/bar lines), so a note and the chord above it always line up.
///
/// What it draws, from chordcore::HarmonyResolver (nothing musical is decided here):
///   * the BACKGROUND of every pitch row is the compatibility with the chord active at that moment: chord tone (strong
///     tint), scale tone (faint tint), chromatic (neutral + hatch), across the whole visible timeline, with the exact
///     tick boundaries of the chord blocks, also while stopped;
///   * each NOTE is coloured by its scale-degree identity (never by the chord). Selection is an outline, velocity is
///     brightness, a locked note carries a small bar.
/// Both are precomputed in setProgression(); paint() only fills rectangles.
///
///   click empty             insert a note at the grid cell and row (length = snap unit, between a 1/16 and a 1/4)
///   click / Shift-click     select / toggle;  drag empty = rectangle select
///   drag a note             move in time and pitch (the selection moves together);  drag an edge = resize
///   Delete, Ctrl+D, Ctrl+C/V, Ctrl+A, arrows (Shift = octave / ...)   as in a piano roll
///   wheel                   scroll pitch;  Shift+wheel scroll time;  Ctrl+wheel zoom time (shared with the grid)
class MelodyRollComponent final : public juce::Component, private juce::Timer {
public:
    MelodyRollComponent(ChordSketchProcessor&, GridComponent&);
    ~MelodyRollComponent() override;

    void setProgression(const chordcore::Progression&);
    void viewChanged() { repaint(); }

    const std::vector<std::size_t>& getSelection() const noexcept { return selection; }
    void setSelectedVelocity(int velocity);
    void toggleLockSelected();
    void deleteSelection();
    void duplicateSelection();
    void copySelection();
    void pasteAt(chordcore::Tick tick);
    void selectAll();
    int selectedCount() const noexcept { return static_cast<int>(selection.size()); }
    /// Velocity of the first selected note, or -1.
    int primaryVelocity() const;
    bool primaryLocked() const;

    std::function<void(const juce::String&)> onMessage;
    std::function<void()> onSelectionChanged;

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
    enum class Gesture { None, Move, ResizeStart, ResizeEnd, Marquee, Insert };

    struct RegionPaint {
        chordcore::Tick start = 0, end = 0;
        std::array<juce::Colour, 12> background;     // by pitch class
        std::array<bool, 12> chromatic{};
        juce::String chordName;
    };

    void timerCallback() override;
    const chordcore::TimelineView& view() const { return grid.getView(); }
    chordcore::Tick unit() const { return grid.getSnapUnit(); }
    juce::Rectangle<float> laneBounds() const { return getLocalBounds().toFloat(); }
    int pitchAtY(float y) const;
    float yOfPitchTop(int pitch) const;
    juce::Rectangle<float> noteBounds(const chordcore::MelodyNote&) const;
    int noteAt(const chordcore::Progression&, juce::Point<float>) const;
    chordcore::Tick insertLength() const;
    void report(const juce::String& text) const { if (onMessage) onMessage(text); }
    void setSelectionInternal(std::vector<std::size_t>, bool notify);
    bool isSelected(std::size_t i) const;
    void clampPitchScroll();
    void ensureNotesVisible();
    void rebuildPaintData();
    void commitGesture();
    void editSelection(const std::string& label, const std::function<chordcore::OpResult(chordcore::Progression&, const std::vector<std::size_t>&)>& op,
                       bool selectResult);
    void drawBackground(juce::Graphics&) const;
    void drawGridLines(juce::Graphics&) const;
    void drawNote(juce::Graphics&, const chordcore::MelodyNote&, bool selected, float alpha) const;

    ChordSketchProcessor& chordProcessor;
    GridComponent& grid;

    chordcore::Progression model;
    chordcore::Progression preview;
    chordcore::HarmonyResolver resolver;
    std::vector<RegionPaint> regions;
    RegionPaint gapPaint;  // no chord: scale tone / chromatic only
    juce::Image hatch;

    std::vector<std::size_t> selection;
    std::vector<std::size_t> previewSelection;
    std::vector<chordcore::MelodyNote> clipboard;

    int lowPitch = 57;  // lowest visible pitch (bottom row)
    bool pitchPlaced = false;

    Gesture gesture = Gesture::None;
    bool dragging = false;
    int pressedNote = -1;
    chordcore::Tick grabOffset = 0;
    int grabPitch = 60;
    chordcore::Tick lastDesiredStart = 0;
    int lastPitchDelta = 0;
    chordcore::Tick lastDesiredEdge = 0;
    int lastAuditionedPitch = -1;
    juce::Point<float> downPosition;
    juce::Rectangle<float> marquee;
    std::vector<std::size_t> selectionAtMarqueeStart;
    juce::Point<float> hover{-1.0f, -1.0f};
    double lastPlayheadX = -1.0;
    bool lastPlayheadPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelodyRollComponent)
};
