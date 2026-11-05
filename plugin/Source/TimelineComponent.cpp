#include "TimelineComponent.h"

#include "UiKit.h"

#include <chordcore/ChordEdit.h>
#include <chordcore/RomanNumeral.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace chordcore;

namespace {

constexpr float kCardGap = 2.0f;          // half the visual gap between cards, per side
constexpr float kEdgeGrabPixels = 8.0f;   // width of the resize zones
constexpr float kDragThreshold = 3.0f;    // pixels before a press becomes a drag

juce::String typeLabel(const ChordSpec& c) {
    juce::String label;
    if (c.suspension == Suspension::Sus2) label = "Sus2";
    else if (c.suspension == Suspension::Sus4) label = "Sus4";
    if (c.seventh != Seventh::None) label = label.isEmpty() ? juce::String("7th") : label + " 7th";
    if (label.isEmpty()) {
        label = c.quality == Quality::Diminished ? "Dim triad" : c.quality == Quality::Augmented ? "Aug triad" : "Triad";
    }
    return label;
}

juce::String barLabel(const Progression& p, Tick start) {
    const Tick bar = p.timeSignature.ticksPerBar();
    const Tick beat = snapUnitTicks(SnapMode::Beat, p.timeSignature);
    const Tick inBar = start % bar;
    const auto number = static_cast<int>(start / bar) + 1;
    if (inBar == 0) return "Bar " + juce::String(number);
    if (inBar % beat == 0) return "Bar " + juce::String(number) + " . " + juce::String(static_cast<int>(inBar / beat) + 1);
    return "Bar " + juce::String(number) + "+";
}

}  // namespace

TimelineComponent::TimelineComponent(ChordSketchProcessor& p) : processor(p) {
    startTimerHz(30);
}

void TimelineComponent::setProgression(const Progression& p) {
    model = p;
    if (!dragging) repaint();
}

void TimelineComponent::setSelectedIndex(int index) {
    selectedIndex = index;
    repaint();
}

void TimelineComponent::select(int index) {
    selectedIndex = index;
    repaint();
    if (onSelectionChanged) onSelectionChanged(index);
}

Tick TimelineComponent::unitTicks() const { return snapUnitTicks(snapMode, model.timeSignature); }

TimelineView TimelineComponent::currentView() const {
    TimelineView view;
    view.originTick = 0;
    view.pixelsPerTick = dragging ? frozenZoom : fitPixelsPerTick(model, static_cast<double>(getWidth()));
    return view;
}

void TimelineComponent::timerCallback() {
    const auto head = processor.getPlayhead();
    const auto view = currentView();
    const double x = head.valid ? view.tickToX(static_cast<Tick>(std::llround(head.ppq * kTicksPerQuarter))) : -1.0;
    if (std::abs(x - lastPlayheadX) > 0.4 || head.playing != lastPlayheadPlaying) {
        lastPlayheadX = x;
        lastPlayheadPlaying = head.playing;
        repaint();
    }
}

void TimelineComponent::paint(juce::Graphics& g) {
    const Progression& shown = dragging ? preview : model;
    const auto view = currentView();
    const Tick bar = shown.timeSignature.ticksPerBar();
    const auto height = static_cast<float>(getHeight());

    // Empty bars: a faint "+" slot (click to add a chord)
    const int visibleBars = static_cast<int>(std::ceil(static_cast<double>(getWidth()) / (view.pixelsPerTick * static_cast<double>(bar)) - 1e-9));
    for (int i = 0; i < visibleBars; ++i) {
        const Tick slotStart = static_cast<Tick>(i) * bar;
        const Tick slotEnd = slotStart + bar;
        const bool occupied = std::any_of(shown.blocks.begin(), shown.blocks.end(), [&](const ChordBlock& b) {
            return b.startTick < slotEnd && b.endTick() > slotStart;
        });
        if (occupied) continue;
        const auto slot = juce::Rectangle<float>(static_cast<float>(view.tickToX(slotStart)) + kCardGap, 0.0f,
                                                 static_cast<float>(view.tickToX(slotEnd) - view.tickToX(slotStart)) - 2 * kCardGap, height);
        g.setColour(ui::controlOutlineColour().withAlpha(0.6f));
        g.drawRoundedRectangle(slot.reduced(0.5f), 12.0f, 1.0f);
        g.setColour(ui::mutedTextColour().withAlpha(0.6f));
        g.setFont(ui::font(28.0f));
        g.drawText("+", slot.toNearestInt(), juce::Justification::centred);
        g.setFont(ui::font(11.0f));
        g.drawText(juce::String(i + 1), slot.toNearestInt().withHeight(18).withTrimmedTop(4), juce::Justification::centred);
    }

    // Cards
    for (std::size_t i = 0; i < shown.blocks.size(); ++i) {
        const auto& block = shown.blocks[i];
        const float x0 = static_cast<float>(view.tickToX(block.startTick));
        const float x1 = static_cast<float>(view.tickToX(block.endTick()));
        const auto card = juce::Rectangle<float>(x0 + kCardGap, 0.0f, std::max(1.0f, x1 - x0 - 2 * kCardGap), height);
        const bool selected = static_cast<int>(i) == selectedIndex;

        auto fill = ui::degreeColour(block.chord.degree);
        g.setColour(selected ? fill.brighter(0.08f) : fill);
        g.fillRoundedRectangle(card, 12.0f);
        if (selected) {
            g.setColour(juce::Colours::white);
            g.drawRoundedRectangle(card.reduced(1.5f), 11.0f, 3.0f);
        }

        const float width = card.getWidth();
        const auto inner = card.toNearestInt();
        const auto romanOpt = formatRomanNumeral(block.chord);
        const auto numeral = ui::displayNumeral(romanOpt ? juce::String(*romanOpt) : juce::String("?"));
        g.setColour(juce::Colours::white);

        if (width >= 52.0f) {
            g.setFont(ui::font(12.0f));
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            g.drawFittedText(barLabel(shown, block.startTick), inner.withTrimmedTop(10).withHeight(16), juce::Justification::centred, 1, 0.7f);
        }
        g.setColour(juce::Colours::white);
        g.setFont(ui::font(width >= 52.0f ? 28.0f : 16.0f, true));
        g.drawFittedText(numeral, inner.withTrimmedTop(width >= 52.0f ? 28 : 0).withHeight(width >= 52.0f ? 36 : getHeight()).reduced(4, 0),
                         juce::Justification::centred, 1, 0.55f);
        if (width >= 52.0f) {
            g.setFont(ui::font(16.0f, true));
            g.drawFittedText(juce::String(chordName(shown.key, block.chord)), inner.withTrimmedTop(66).withHeight(20).reduced(4, 0),
                             juce::Justification::centred, 1, 0.6f);
            g.setFont(ui::font(12.0f));
            g.setColour(juce::Colours::white.withAlpha(0.85f));
            auto label = typeLabel(block.chord);
            if (block.octave != 0) label += juce::String(juce::CharPointer_UTF8(" \xC2\xB7 ")) + (block.octave > 0 ? "+" : "") + juce::String(block.octave);
            g.drawFittedText(label, inner.withTrimmedTop(88).withHeight(16).reduced(4, 0), juce::Justification::centred, 1, 0.6f);
        }
    }

    // Host playhead
    const auto head = processor.getPlayhead();
    if (head.valid) {
        const auto x = static_cast<float>(view.tickToX(static_cast<Tick>(std::llround(head.ppq * kTicksPerQuarter))));
        if (x >= 0.0f && x <= static_cast<float>(getWidth())) {
            g.setColour(juce::Colours::white.withAlpha(head.playing ? 0.95f : 0.4f));
            g.fillRect(x - 1.0f, 0.0f, 2.0f, height);
        }
    }
}

void TimelineComponent::mouseMove(const juce::MouseEvent& e) {
    const auto hit = chordcore::hitTest(model, currentView(), e.position.x, kEdgeGrabPixels);
    setMouseCursor(hit.zone == HitZone::LeftEdge || hit.zone == HitZone::RightEdge ? juce::MouseCursor::LeftRightResizeCursor
                                                                                   : juce::MouseCursor::NormalCursor);
}

void TimelineComponent::mouseDown(const juce::MouseEvent& e) {
    const auto view = currentView();
    const auto hit = chordcore::hitTest(model, view, e.position.x, kEdgeGrabPixels);
    gestureActive = true;
    dragging = false;
    pressedOnSlot = false;
    downPosition = e.position;

    if (hit.index >= 0) {
        select(hit.index);
        dragIndex = hit.index;
        dragKind = hit.zone == HitZone::LeftEdge ? DragKind::ResizeStart : hit.zone == HitZone::RightEdge ? DragKind::ResizeEnd : DragKind::Move;
        frozenZoom = view.pixelsPerTick;
        grabOffset = view.xToTick(e.position.x) - model.blocks[static_cast<std::size_t>(hit.index)].startTick;
        preview = model;
        return;
    }

    dragKind = DragKind::None;
    dragIndex = -1;
    const Tick bar = model.timeSignature.ticksPerBar();
    const Tick tick = std::max<Tick>(view.xToTick(e.position.x), 0);
    const Tick slotStart = (tick / bar) * bar;
    const bool occupied = std::any_of(model.blocks.begin(), model.blocks.end(), [&](const ChordBlock& b) {
        return b.startTick < slotStart + bar && b.endTick() > slotStart;
    });
    if (!occupied) {
        pressedOnSlot = true;
        pressedSlotTick = slotStart;
    }
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& e) {
    if (!gestureActive || dragKind == DragKind::None) return;
    if (!dragging) {
        if (e.position.getDistanceFrom(downPosition) < kDragThreshold) return;
        dragging = true;
    }

    const auto view = currentView();  // frozen zoom while dragging
    const Tick unit = unitTicks();
    const Tick tick = view.xToTick(e.position.x);
    const auto index = static_cast<std::size_t>(dragIndex);

    preview = model;
    switch (dragKind) {
        case DragKind::Move: moveBlock(preview, index, tick - grabOffset, unit); break;
        case DragKind::ResizeStart: resizeBlockStart(preview, index, tick, unit); break;
        case DragKind::ResizeEnd: resizeBlockEnd(preview, index, tick, unit); break;
        case DragKind::None: break;
    }
    repaint();
}

void TimelineComponent::mouseUp(const juce::MouseEvent&) {
    if (!gestureActive) return;
    const bool wasDragging = dragging;
    const bool slotClick = pressedOnSlot && !dragging;
    const Tick slotTick = pressedSlotTick;
    gestureActive = false;

    if (wasDragging) {
        commitDrag();
    } else if (slotClick) {
        commitAdd(slotTick);
    }
    dragging = false;
    dragKind = DragKind::None;
    pressedOnSlot = false;
    repaint();
}

void TimelineComponent::commitDrag() {
    const auto index = static_cast<std::size_t>(dragIndex);
    if (index >= preview.blocks.size() || preview.blocks == model.blocks) return;

    const Tick start = preview.blocks[index].startTick;
    const Tick duration = preview.blocks[index].durationTicks;
    const char* label = dragKind == DragKind::Move ? "Move chord" : "Resize chord";

    dragging = false;  // from here the committed model is what is shown
    const auto result = processor.editProgression(label, [index, start, duration](Progression& p) {
        auto& block = p.blocks.at(index);
        block.startTick = start;
        block.durationTicks = duration;
    });
    model = processor.getProgression();
    if (!result.ok && onMessage) onMessage("Cannot do that: " + juce::String(result.error));
}

void TimelineComponent::commitAdd(Tick atTick) {
    const auto result = processor.editProgression("Add chord", [atTick](Progression& p) {
        const auto r = addBlock(p, diatonicChord(p.key.mode, 1), atTick, 1);
        if (!r.ok) throw std::runtime_error(r.error);
    });
    if (!result.ok) {
        if (onMessage) onMessage("Cannot add a chord there: " + juce::String(result.error));
        return;
    }
    model = processor.getProgression();
    select(findBlockAt(model, atTick));
}
