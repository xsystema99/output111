#include "GridComponent.h"

#include "UiKit.h"

#include <chordcore/ChordEdit.h>
#include <chordcore/RomanNumeral.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace chordcore;

namespace {

constexpr int kRulerHeight = 34;
constexpr int kScrollBarHeight = 14;
constexpr float kLanePad = 6.0f;
constexpr float kEdgeGrabPixels = 8.0f;
constexpr float kDragThreshold = 3.0f;

int textWidth(const juce::Font& font, const juce::String& text) {
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, 0.0f);
    return static_cast<int>(std::ceil(glyphs.getBoundingBox(0, -1, true).getWidth()));
}

juce::String typeLabel(const ChordSpec& c) {
    juce::String label;
    if (c.suspension == Suspension::Sus2) label = "sus2";
    else if (c.suspension == Suspension::Sus4) label = "sus4";
    if (c.seventh != Seventh::None) {
        const bool altered = c.ninth == Ninth::Minor || c.ninth == Ninth::Sharp || c.eleventh == Eleventh::Sharp || c.thirteenth == Thirteenth::Minor;
        juce::String ext = c.thirteenth != Thirteenth::None ? "13th" : c.eleventh != Eleventh::None ? "11th" : c.ninth != Ninth::None ? "9th" : "7th";
        if (altered) ext += " alt";
        label = label.isEmpty() ? ext : label + " " + ext;
    }
    return label;
}

}  // namespace

GridComponent::GridComponent(ChordSketchProcessor& p) : chordProcessor(p) {
    setWantsKeyboardFocus(true);
    scrollBar.setAutoHide(false);
    scrollBar.addListener(this);
    scrollBar.setColour(juce::ScrollBar::thumbColourId, ui::controlOutlineColour());
    addAndMakeVisible(scrollBar);
    currentView.pixelsPerTick = 0.05;
    startTimerHz(30);
}

GridComponent::~GridComponent() { scrollBar.removeListener(this); }

// ------------------------------------------------------------------------------------------ geometry

juce::Rectangle<int> GridComponent::rulerBounds() const { return {0, 0, getWidth(), kRulerHeight}; }

juce::Rectangle<int> GridComponent::laneBounds() const {
    return {0, kRulerHeight, getWidth(), std::max(0, getHeight() - kRulerHeight - kScrollBarHeight)};
}

juce::Rectangle<float> GridComponent::blockBounds(const ChordBlock& b) const {
    const auto lane = laneBounds().toFloat();
    const auto x0 = static_cast<float>(currentView.tickToX(b.startTick));
    const auto x1 = static_cast<float>(currentView.tickToX(b.endTick()));
    return {x0 + 1.0f, lane.getY() + kLanePad, std::max(2.0f, x1 - x0 - 2.0f), lane.getHeight() - 2.0f * kLanePad};
}

Tick GridComponent::cellStart(Tick tick) const {
    const Tick u = std::max<Tick>(unit(), 1);
    return (std::max<Tick>(tick, 0) / u) * u;
}

bool GridComponent::isSelected(std::size_t index) const { return std::find(selection.begin(), selection.end(), index) != selection.end(); }

void GridComponent::applyView(TimelineView next, bool userAction) {
    currentView = clampView(next, extent(), laneWidth());
    if (userAction) userZoomed = true;
    updateScrollBar();
    repaint();
}

void GridComponent::updateScrollBar() {
    const double ppt = std::max(currentView.pixelsPerTick, 1e-9);
    const double visible = laneWidth() / ppt;
    scrollBar.setRangeLimits(0.0, static_cast<double>(extent()));
    scrollBar.setCurrentRange(static_cast<double>(currentView.originTick), visible, juce::dontSendNotification);
}

void GridComponent::scrollBarMoved(juce::ScrollBar*, double newRangeStart) {
    auto next = currentView;
    next.originTick = static_cast<Tick>(std::llround(newRangeStart));
    currentView = clampView(next, extent(), laneWidth());
    userZoomed = true;
    repaint();
}

void GridComponent::fitToWindow() {
    userZoomed = false;
    applyView(fitView(model, laneWidth()), false);
}

void GridComponent::resized() {
    scrollBar.setBounds(0, getHeight() - kScrollBarHeight, getWidth(), kScrollBarHeight);
    if (userZoomed) applyView(currentView, false);
    else applyView(fitView(model, laneWidth()), false);
}

void GridComponent::setSnapMode(SnapMode mode) {
    snapMode = mode;  // only the snap and the ruler subdivisions change; no chord moves
    repaint();
}

void GridComponent::setProgression(const Progression& p) {
    model = p;
    if (gesture == Gesture::None || !dragging) {
        // drop selection indices that no longer exist
        selection.erase(std::remove_if(selection.begin(), selection.end(), [&](std::size_t i) { return i >= model.blocks.size(); }), selection.end());
        if (primary >= static_cast<int>(model.blocks.size())) primary = selection.empty() ? -1 : static_cast<int>(selection.back());
    }
    if (!userZoomed) currentView = clampView(fitView(model, laneWidth()), extent(), laneWidth());
    else currentView = clampView(currentView, extent(), laneWidth());
    updateScrollBar();
    repaint();
}

void GridComponent::setSelection(std::vector<std::size_t> indices, int newPrimary) { setSelectionInternal(std::move(indices), newPrimary, false); }

void GridComponent::setSelectionInternal(std::vector<std::size_t> indices, int newPrimary, bool notify) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    selection = std::move(indices);
    primary = selection.empty() ? -1 : newPrimary;
    if (primary >= 0 && !isSelected(static_cast<std::size_t>(primary))) primary = static_cast<int>(selection.back());
    repaint();
    if (notify && onSelectionChanged) onSelectionChanged(selection, primary);
}

// ------------------------------------------------------------------------------------------ playhead

void GridComponent::timerCallback() {
    const auto head = chordProcessor.getPlayhead();
    double x = -1.0;
    juce::String text;
    if (head.valid) {
        const auto tick = static_cast<Tick>(std::llround(head.ppq * kTicksPerQuarter));
        x = currentView.tickToX(tick);
        text = juce::String(positionLabel(model.timeSignature, tick));
        // follow the playhead while the host plays (not while the user is editing)
        if (head.playing && gesture == Gesture::None && (x < 0.0 || x > laneWidth())) {
            auto next = currentView;
            next.originTick = std::max<Tick>(0, tick - static_cast<Tick>(laneWidth() * 0.1 / currentView.pixelsPerTick));
            currentView = clampView(next, extent(), laneWidth());
            updateScrollBar();
            x = currentView.tickToX(tick);
        }
    }
    if (std::abs(x - lastPlayheadX) > 0.4 || head.playing != lastPlayheadPlaying || text != lastPlayheadText) {
        lastPlayheadX = x;
        lastPlayheadPlaying = head.playing;
        lastPlayheadText = text;
        repaint();
    }
}

// ------------------------------------------------------------------------------------------ painting

void GridComponent::drawRuler(juce::Graphics& g) const {
    const auto ruler = rulerBounds();
    g.setColour(ui::controlFillColour());
    g.fillRect(ruler);
    g.setColour(ui::panelBorderColour());
    g.fillRect(0, kRulerHeight - 1, getWidth(), 1);

    const double ppt = currentView.pixelsPerTick;
    const Tick first = std::max<Tick>(0, currentView.xToTick(-40.0));
    const Tick last = currentView.xToTick(laneWidth() + 40.0);
    const auto marks = buildRuler(model.timeSignature, first, last, ppt, unit());

    for (const auto& m : marks) {
        const auto x = static_cast<float>(currentView.tickToX(m.tick));
        if (m.level == MarkLevel::Bar) {
            if (m.showLine) {
                g.setColour(ui::textColour().withAlpha(0.75f));
                g.fillRect(x - 0.5f, 2.0f, 1.0f, static_cast<float>(kRulerHeight - 3));
            }
            if (m.showLabel) {
                g.setColour(ui::textColour());
                g.setFont(ui::font(13.0f, true));
                g.drawText(juce::String(m.label), juce::Rectangle<float>(x + 4.0f, 2.0f, 40.0f, 15.0f), juce::Justification::centredLeft);
            }
        } else if (m.level == MarkLevel::Beat) {
            if (m.showLine) {
                g.setColour(ui::mutedTextColour().withAlpha(0.7f));
                g.fillRect(x - 0.5f, 14.0f, 1.0f, static_cast<float>(kRulerHeight - 15));
            }
            if (m.showLabel) {
                g.setColour(ui::mutedTextColour());
                g.setFont(ui::font(11.0f));
                g.drawText(juce::String(m.label), juce::Rectangle<float>(x + 3.0f, 17.0f, 30.0f, 14.0f), juce::Justification::centredLeft);
            }
        } else if (m.showLine) {
            g.setColour(ui::mutedTextColour().withAlpha(0.35f));
            g.fillRect(x - 0.5f, 24.0f, 1.0f, static_cast<float>(kRulerHeight - 25));
            if (m.showLabel) {
                g.setColour(ui::mutedTextColour().withAlpha(0.8f));
                g.setFont(ui::font(10.0f));
                g.drawText(juce::String(m.label), juce::Rectangle<float>(x + 2.0f, 17.0f, 20.0f, 13.0f), juce::Justification::centredLeft);
            }
        }
    }
}

void GridComponent::drawBlock(juce::Graphics& g, const ChordBlock& block, bool selected, float alpha) const {
    const auto r = blockBounds(block);
    if (r.getRight() < 0.0f || r.getX() > static_cast<float>(getWidth())) return;
    auto fill = ui::degreeColour(block.chord.degree).withAlpha(alpha);
    g.setColour(selected ? fill.brighter(0.08f) : fill);
    g.fillRoundedRectangle(r, 6.0f);
    if (selected) {
        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.drawRoundedRectangle(r.reduced(1.0f), 5.0f, 2.0f);
    }
    const auto inner = r.toNearestInt();
    if (inner.getWidth() < 22) return;
    const auto romanOpt = formatRomanNumeral(block.chord);
    const auto numeral = ui::displayNumeral(romanOpt ? juce::String(*romanOpt) : juce::String("?"));
    const bool roomy = inner.getWidth() >= 54 && inner.getHeight() >= 60;

    g.setColour(juce::Colours::white.withAlpha(alpha));
    g.setFont(ui::font(roomy ? 16.0f : 13.0f, true));
    g.drawFittedText(juce::String(chordName(model.key, block.chord)), inner.reduced(4, 0).withTrimmedTop(roomy ? 8 : 2).withHeight(roomy ? 22 : inner.getHeight() / 2),
                     juce::Justification::centred, 1, 0.6f);
    g.setFont(ui::font(roomy ? 20.0f : 12.0f, true));
    g.drawFittedText(numeral, inner.reduced(4, 0).withTrimmedTop(roomy ? 32 : inner.getHeight() / 2).withHeight(roomy ? 26 : inner.getHeight() / 2),
                     juce::Justification::centred, 1, 0.55f);
    if (roomy) {
        auto label = typeLabel(block.chord);
        if (block.chord.inversion != 0) label += juce::String(juce::CharPointer_UTF8(label.isEmpty() ? "inv " : " \xC2\xB7 inv ")) + juce::String(block.chord.inversion);
        if (block.octave != 0) label += juce::String(juce::CharPointer_UTF8(label.isEmpty() ? "" : " \xC2\xB7 ")) + "oct " + (block.octave > 0 ? "+" : "") + juce::String(block.octave);
        if (label.isNotEmpty()) {
            g.setFont(ui::font(11.0f));
            g.setColour(juce::Colours::white.withAlpha(0.85f * alpha));
            g.drawFittedText(label, inner.reduced(4, 0).withTrimmedTop(60).withHeight(16), juce::Justification::centred, 1, 0.6f);
        }
    }
}

void GridComponent::drawLane(juce::Graphics& g) const {
    const auto lane = laneBounds();
    g.setColour(ui::controlFillColour().darker(0.3f));
    g.fillRect(lane);

    // grid lines: bar strongest, beat medium, subdivisions light
    const Tick first = std::max<Tick>(0, currentView.xToTick(-40.0));
    const Tick last = currentView.xToTick(laneWidth() + 40.0);
    const auto marks = buildRuler(model.timeSignature, first, last, currentView.pixelsPerTick, unit());
    for (const auto& m : marks) {
        if (!m.showLine) continue;
        const auto x = static_cast<float>(currentView.tickToX(m.tick));
        const float alpha = m.level == MarkLevel::Bar ? 0.75f : m.level == MarkLevel::Beat ? 0.38f : 0.16f;
        g.setColour(ui::textColour().withAlpha(alpha * 0.6f));
        g.fillRect(x - 0.5f, static_cast<float>(lane.getY()), m.level == MarkLevel::Bar ? 1.5f : 1.0f, static_cast<float>(lane.getHeight()));
    }

    const bool moving = dragging && (gesture == Gesture::Move);
    const Progression& shown = dragging && gesture != Gesture::Marquee ? preview : model;

    // originals of a move stay visible as faint outlines
    if (moving) {
        for (auto i : selection) {
            if (i >= model.blocks.size()) continue;
            g.setColour(ui::mutedTextColour().withAlpha(0.45f));
            g.drawRoundedRectangle(blockBounds(model.blocks[i]).reduced(0.5f), 6.0f, 1.0f);
        }
    }

    for (std::size_t i = 0; i < shown.blocks.size(); ++i) {
        const bool sel = isSelected(i);
        drawBlock(g, shown.blocks[i], sel, moving && sel ? 0.78f : 1.0f);
    }

    // destination readout while moving or resizing
    if (dragging && primary >= 0 && static_cast<std::size_t>(primary) < shown.blocks.size() &&
        (gesture == Gesture::Move || gesture == Gesture::ResizeStart || gesture == Gesture::ResizeEnd)) {
        const auto& b = shown.blocks[static_cast<std::size_t>(primary)];
        juce::String text = juce::String(positionLabel(model.timeSignature, b.startTick));
        text += juce::String(juce::CharPointer_UTF8("  \xC2\xB7  ")) + juce::String(b.durationTicks) + "t";
        g.setFont(ui::font(11.0f, true));
        const int w = textWidth(g.getCurrentFont(), text) + 14;
        auto pill = juce::Rectangle<float>(static_cast<float>(std::clamp(static_cast<int>(blockBounds(b).getX()), 2, std::max(2, getWidth() - w - 2))),
                                           static_cast<float>(lane.getY()) + 1.0f, static_cast<float>(w), 16.0f);
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.fillRoundedRectangle(pill, 8.0f);
        g.setColour(juce::Colours::white);
        g.drawText(text, pill, juce::Justification::centred);
    }

    // hover: where a click would insert
    if (gesture == Gesture::None && hoverPosition.x >= 0.0f && laneBounds().toFloat().contains(hoverPosition)) {
        const Tick cell = cellStart(currentView.xToTick(hoverPosition.x));
        Progression trial = model;
        const auto r = addBlock(trial, insertChord, cell, unit(), insertDuration);
        if (r.ok) {
            const auto& b = trial.blocks[r.index];
            const auto box = blockBounds(b);
            g.setColour(ui::degreeColour(insertChord.degree).withAlpha(0.28f));
            g.fillRoundedRectangle(box, 6.0f);
            g.setColour(ui::degreeColour(insertChord.degree).withAlpha(0.9f));
            g.drawRoundedRectangle(box.reduced(0.5f), 6.0f, 1.0f);
            g.setFont(ui::font(11.0f, true));
            g.setColour(juce::Colours::white.withAlpha(0.9f));
            g.drawText("+ " + juce::String(chordName(model.key, insertChord)), box.toNearestInt().withHeight(18), juce::Justification::centred);
        }
    }

    if (gesture == Gesture::Marquee && dragging) {
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRect(marquee);
        g.setColour(juce::Colours::white.withAlpha(0.7f));
        g.drawRect(marquee, 1.0f);
    }
}

void GridComponent::paint(juce::Graphics& g) {
    g.fillAll(ui::controlFillColour());
    drawRuler(g);
    drawLane(g);

    const auto head = chordProcessor.getPlayhead();
    if (head.valid) {
        const auto tick = static_cast<Tick>(std::llround(head.ppq * kTicksPerQuarter));
        const auto x = static_cast<float>(currentView.tickToX(tick));
        if (x >= 0.0f && x <= static_cast<float>(getWidth())) {
            g.setColour(juce::Colours::white.withAlpha(head.playing ? 0.95f : 0.45f));
            g.fillRect(x - 1.0f, 0.0f, 2.0f, static_cast<float>(getHeight() - kScrollBarHeight));
        }
        if (lastPlayheadText.isNotEmpty()) {
            g.setFont(ui::font(11.0f, true));
            const int w = textWidth(g.getCurrentFont(), lastPlayheadText) + 14;
            const auto pill = juce::Rectangle<float>(static_cast<float>(getWidth() - w - 4), 2.0f, static_cast<float>(w), 15.0f);
            g.setColour(juce::Colours::black.withAlpha(0.8f));
            g.fillRoundedRectangle(pill, 7.5f);
            g.setColour(head.playing ? juce::Colours::white : ui::mutedTextColour());
            g.drawText(lastPlayheadText, pill, juce::Justification::centred);
        }
    }
    g.setColour(ui::panelBorderColour());
    g.drawRect(getLocalBounds(), 1);
}

// ------------------------------------------------------------------------------------------ mouse

void GridComponent::mouseMove(const juce::MouseEvent& e) {
    hoverPosition = e.position;
    if (laneBounds().contains(e.getPosition())) {
        const auto hit = chordcore::hitTest(model, currentView, e.position.x, kEdgeGrabPixels);
        setMouseCursor(hit.zone == HitZone::LeftEdge || hit.zone == HitZone::RightEdge ? juce::MouseCursor::LeftRightResizeCursor
                       : hit.index >= 0                                                ? juce::MouseCursor::NormalCursor
                                                                                       : juce::MouseCursor::PointingHandCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    repaint();
}

void GridComponent::mouseExit(const juce::MouseEvent&) {
    hoverPosition = {-1.0f, -1.0f};
    repaint();
}

void GridComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    gesture = Gesture::None;
    dragging = false;
    pressedBlock = -1;
    if (!laneBounds().contains(e.getPosition())) return;  // the ruler is not interactive
    beginDragAutoRepeat(30);

    downPosition = dragPosition = e.position;
    const auto hit = chordcore::hitTest(model, currentView, e.position.x, kEdgeGrabPixels);

    if (hit.index >= 0) {
        const auto idx = static_cast<std::size_t>(hit.index);
        pressedBlock = hit.index;
        pressedWasSelected = isSelected(idx);
        if (e.mods.isShiftDown()) {  // toggle membership; no drag
            auto next = selection;
            if (pressedWasSelected) next.erase(std::remove(next.begin(), next.end(), idx), next.end());
            else next.push_back(idx);
            setSelectionInternal(next, pressedWasSelected ? (next.empty() ? -1 : static_cast<int>(next.back())) : hit.index, true);
            return;
        }
        if (!pressedWasSelected || hit.zone != HitZone::Body) setSelectionInternal({idx}, hit.index, true);
        else { primary = hit.index; if (onSelectionChanged) onSelectionChanged(selection, primary); }
        gesture = hit.zone == HitZone::LeftEdge ? Gesture::ResizeStart : hit.zone == HitZone::RightEdge ? Gesture::ResizeEnd : Gesture::Move;
        grabOffset = currentView.xToTick(e.position.x) - model.blocks[idx].startTick;
        preview = model;
        return;
    }

    // empty lane: a click inserts, a drag selects a rectangle
    pressedCell = cellStart(currentView.xToTick(e.position.x));
    gesture = Gesture::InsertClick;
    selectionAtMarqueeStart = e.mods.isShiftDown() ? selection : std::vector<std::size_t>{};
}

void GridComponent::mouseDrag(const juce::MouseEvent& e) {
    if (gesture == Gesture::None) return;
    dragPosition = e.position;
    if (!dragging) {
        if (e.position.getDistanceFrom(downPosition) < kDragThreshold) return;
        dragging = true;
        if (gesture == Gesture::InsertClick) gesture = Gesture::Marquee;
    }

    // edge auto-scroll while dragging
    const float x = e.position.x;
    if (x < 16.0f) applyView([&] { auto v = currentView; v.originTick -= static_cast<Tick>(10.0 / currentView.pixelsPerTick); return v; }(), true);
    else if (x > static_cast<float>(getWidth()) - 16.0f) applyView([&] { auto v = currentView; v.originTick += static_cast<Tick>(10.0 / currentView.pixelsPerTick); return v; }(), true);

    const Tick tick = currentView.xToTick(x);
    if (gesture == Gesture::Marquee) {
        marquee = juce::Rectangle<float>(downPosition, e.position).getIntersection(laneBounds().toFloat());
        std::vector<std::size_t> hits = selectionAtMarqueeStart;
        for (std::size_t i = 0; i < model.blocks.size(); ++i) {
            const auto b = blockBounds(model.blocks[i]);
            if (b.getRight() >= marquee.getX() && b.getX() <= marquee.getRight()) hits.push_back(i);
        }
        setSelectionInternal(hits, hits.empty() ? -1 : static_cast<int>(hits.back()), false);
        return;
    }

    if (pressedBlock < 0) return;
    preview = model;
    const auto anchor = static_cast<std::size_t>(pressedBlock);
    switch (gesture) {
        case Gesture::Move: moveBlocks(preview, selection, anchor, tick - grabOffset, unit()); break;
        case Gesture::ResizeStart: resizeBlockStart(preview, anchor, tick, unit()); break;
        case Gesture::ResizeEnd: resizeBlockEnd(preview, anchor, tick, unit()); break;
        default: break;
    }
    repaint();
}

void GridComponent::mouseUp(const juce::MouseEvent& e) {
    const Gesture g = gesture;
    const bool wasDragging = dragging;
    gesture = Gesture::None;

    if (g == Gesture::Marquee) {
        dragging = false;
        if (onSelectionChanged) onSelectionChanged(selection, primary);
    } else if (g == Gesture::InsertClick && !wasDragging) {
        dragging = false;
        commitInsert(pressedCell);
    } else if ((g == Gesture::Move || g == Gesture::ResizeStart || g == Gesture::ResizeEnd) && wasDragging) {
        gesture = g;  // commitGesture still needs it
        commitGesture();
        gesture = Gesture::None;
    } else if (g == Gesture::Move && !wasDragging && pressedWasSelected && selection.size() > 1 && !e.mods.isShiftDown()) {
        setSelectionInternal({static_cast<std::size_t>(pressedBlock)}, pressedBlock, true);  // plain click inside a group
    }
    dragging = false;
    pressedBlock = -1;
    repaint();
}

void GridComponent::commitGesture() {
    if (pressedBlock < 0 || preview.blocks == model.blocks) { dragging = false; return; }
    const auto anchor = static_cast<std::size_t>(pressedBlock);
    if (anchor >= preview.blocks.size()) { dragging = false; return; }
    const Tick start = preview.blocks[anchor].startTick;
    const Tick end = preview.blocks[anchor].endTick();
    const auto sel = selection;
    const Tick u = unit();
    const Gesture g = gesture;
    // Replays the same call that produced the preview, on the current document, as ONE undoable edit.
    const auto result = chordProcessor.editProgression(g == Gesture::Move ? (sel.size() > 1 ? "Move chords" : "Move chord") : "Resize chord",
                                                       [=](Progression& p) {
        OpResult r;
        if (g == Gesture::Move) r = moveBlocks(p, sel, anchor, start, u);
        else if (g == Gesture::ResizeStart) r = resizeBlockStart(p, anchor, start, u);
        else r = resizeBlockEnd(p, anchor, end, u);
        if (!r.ok) throw std::runtime_error(r.error);
    });
    dragging = false;
    model = chordProcessor.getProgression();
    if (!result.ok) report("Cannot do that: " + juce::String(result.error));
}

void GridComponent::commitInsert(Tick atTick) {
    const ChordSpec chord = insertChord;
    const Tick u = unit(), dur = insertDuration;
    std::size_t newIndex = 0;
    const auto result = chordProcessor.editProgression("Add chord", [&](Progression& p) {
        const auto r = addBlock(p, chord, atTick, u, dur);
        if (!r.ok) throw std::runtime_error(r.error);
        newIndex = r.index;
    });
    if (!result.ok) {
        report("Not added: " + juce::String(result.error) + " (nothing was overwritten)");
        return;
    }
    model = chordProcessor.getProgression();
    setSelectionInternal({newIndex}, static_cast<int>(newIndex), true);
    report("Added " + juce::String(chordName(model.key, chord)) + " at " + juce::String(positionLabel(model.timeSignature, atTick)));
}

void GridComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (gesture != Gesture::None) return;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown()) {
        const double factor = std::exp(static_cast<double>(w.deltaY) * 1.2);
        applyView(zoomView(currentView, static_cast<double>(e.position.x), factor, extent(), laneWidth()), true);
        return;
    }
    const float delta = (e.mods.isShiftDown() || std::abs(w.deltaX) < 1e-6f) ? w.deltaY : w.deltaX;
    applyView(scrollView(currentView, -static_cast<double>(delta) * 400.0, extent(), laneWidth()), true);
}

// ------------------------------------------------------------------------------------------ commands

void GridComponent::deleteSelection() {
    if (selection.empty()) return;
    const auto sel = selection;
    const auto result = chordProcessor.editProgression(sel.size() > 1 ? "Delete chords" : "Delete chord", [sel](Progression& p) {
        const auto r = deleteBlocks(p, sel);
        if (!r.ok) throw std::runtime_error(r.error);
    });
    if (result.ok) { model = chordProcessor.getProgression(); setSelectionInternal({}, -1, true); }
    else report("Cannot delete: " + juce::String(result.error));
}

void GridComponent::duplicateSelection() {
    if (selection.empty()) return;
    const auto sel = selection;
    std::vector<std::size_t> copies;
    const auto result = chordProcessor.editProgression(sel.size() > 1 ? "Duplicate chords" : "Duplicate chord", [&](Progression& p) {
        const auto r = duplicateBlocks(p, sel);
        if (!r.ok) throw std::runtime_error(r.error);
        copies = r.indices;
    });
    if (result.ok) {
        model = chordProcessor.getProgression();
        setSelectionInternal(copies, copies.empty() ? -1 : static_cast<int>(copies.back()), true);
    } else {
        report("Cannot duplicate: " + juce::String(result.error));
    }
}

bool GridComponent::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (key == juce::KeyPress::escapeKey) { setSelectionInternal({}, -1, true); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (mods.isCommandDown() && (key.getKeyCode() == 'D' || key.getKeyCode() == 'd')) { duplicateSelection(); return true; }
    if (mods.isCommandDown() && (key.getKeyCode() == 'A' || key.getKeyCode() == 'a')) {
        std::vector<std::size_t> all(model.blocks.size());
        for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
        setSelectionInternal(all, all.empty() ? -1 : static_cast<int>(all.back()), true);
        return true;
    }
    return false;
}
