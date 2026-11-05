#include "MelodyRollComponent.h"

#include "UiKit.h"

#include <chordcore/ChordEdit.h>
#include <chordcore/Grid.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace chordcore;

namespace {

constexpr float kRowHeight = 9.0f;
constexpr float kEdgeGrab = 6.0f;
constexpr float kDragThreshold = 3.0f;

juce::Colour toColour(const Rgb& c) { return juce::Colour(c.r, c.g, c.b); }
juce::Colour toColour(const Rgba& c) { return juce::Colour::fromRGBA(c.r, c.g, c.b, c.a); }

juce::String pitchName(const Key& key, int pitch) {
    return juce::String(noteName(key, ((pitch % 12) + 12) % 12)) + juce::String(pitch / 12 - 1);
}

juce::Image makeHatch() {
    juce::Image image(juce::Image::ARGB, 6, 6, true);
    for (int i = 0; i < 6; ++i) image.setPixelAt(i, 5 - i, juce::Colour::fromRGBA(255, 255, 255, 34));
    return image;
}

}  // namespace

MelodyRollComponent::MelodyRollComponent(ChordSketchProcessor& p, GridComponent& g) : chordProcessor(p), grid(g), hatch(makeHatch()) {
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

MelodyRollComponent::~MelodyRollComponent() = default;

// ------------------------------------------------------------------------------------------ geometry

int MelodyRollComponent::pitchAtY(float y) const {
    const int row = static_cast<int>(std::floor((static_cast<float>(getHeight()) - y) / kRowHeight));
    return std::clamp(lowPitch + row, 0, 127);
}

float MelodyRollComponent::yOfPitchTop(int pitch) const {
    return static_cast<float>(getHeight()) - static_cast<float>(pitch - lowPitch + 1) * kRowHeight;
}

juce::Rectangle<float> MelodyRollComponent::noteBounds(const MelodyNote& n) const {
    const auto x0 = static_cast<float>(view().tickToX(n.startTick));
    const auto x1 = static_cast<float>(view().tickToX(n.endTick()));
    return {x0 + 0.5f, yOfPitchTop(n.pitch) + 1.0f, std::max(2.0f, x1 - x0 - 1.0f), kRowHeight - 2.0f};
}

int MelodyRollComponent::noteAt(const Progression& p, juce::Point<float> pos) const {
    for (int i = static_cast<int>(p.melody.size()) - 1; i >= 0; --i) {
        if (noteBounds(p.melody[static_cast<std::size_t>(i)]).expanded(0.0f, 0.5f).contains(pos)) return i;
    }
    return -1;
}

Tick MelodyRollComponent::insertLength() const { return std::clamp<Tick>(unit(), 240, 960); }

bool MelodyRollComponent::isSelected(std::size_t i) const { return std::find(selection.begin(), selection.end(), i) != selection.end(); }

void MelodyRollComponent::clampPitchScroll() {
    const int rows = std::max(1, static_cast<int>(static_cast<float>(getHeight()) / kRowHeight));
    lowPitch = std::clamp(lowPitch, 0, std::max(0, 128 - rows));
}

void MelodyRollComponent::resized() {
    if (!pitchPlaced) ensureNotesVisible();
    clampPitchScroll();
}

void MelodyRollComponent::ensureNotesVisible() {
    const int rows = std::max(1, static_cast<int>(static_cast<float>(getHeight()) / kRowHeight));
    if (model.melody.empty() || getHeight() <= 0) return;
    long sum = 0;
    for (const auto& n : model.melody) sum += n.pitch;
    const int mean = static_cast<int>(sum / static_cast<long>(model.melody.size()));
    lowPitch = mean - rows / 2;
    pitchPlaced = true;
    clampPitchScroll();
}

// ------------------------------------------------------------------------------------------ model

void MelodyRollComponent::setProgression(const Progression& p) {
    model = p;
    if (!dragging) {
        selection.erase(std::remove_if(selection.begin(), selection.end(), [&](std::size_t i) { return i >= model.melody.size(); }), selection.end());
    }
    if (!pitchPlaced) ensureNotesVisible();
    rebuildPaintData();
    repaint();
}

void MelodyRollComponent::rebuildPaintData() {
    HarmonicSettings settings;
    settings.mapping = model.melodySettings.mapping;
    settings.palette = model.melodySettings.palette;
    resolver = HarmonyResolver(model.key, settings);

    auto fill = [&](RegionPaint& rp, std::uint16_t mask) {
        for (int pc = 0; pc < 12; ++pc) {
            const Compat c = resolver.classify(pc, mask);
            rp.background[static_cast<std::size_t>(pc)] = toColour(HarmonyResolver::backgroundFor(c, resolver.identity(pc).colour));
            rp.chromatic[static_cast<std::size_t>(pc)] = (c == Compat::Chromatic);
        }
    };

    regions.clear();
    for (const auto& region : buildCompatibility(model)) {
        RegionPaint rp;
        rp.start = region.startTick;
        rp.end = region.endTick;
        fill(rp, region.chordMask);
        rp.chordName = juce::String(chordName(model.key, model.blocks[region.blockIndex].chord));
        regions.push_back(std::move(rp));
    }
    gapPaint = RegionPaint{};
    fill(gapPaint, 0);
}

// ------------------------------------------------------------------------------------------ playhead

void MelodyRollComponent::timerCallback() {
    const auto head = chordProcessor.getPlayhead();
    const double x = head.valid ? view().tickToX(static_cast<Tick>(std::llround(head.ppq * kTicksPerQuarter))) : -1.0;
    if (std::abs(x - lastPlayheadX) > 0.4 || head.playing != lastPlayheadPlaying) {
        lastPlayheadX = x;
        lastPlayheadPlaying = head.playing;
        repaint();
    }
}

// ------------------------------------------------------------------------------------------ painting

void MelodyRollComponent::drawBackground(juce::Graphics& g) const {
    const float width = static_cast<float>(getWidth());
    const Tick first = std::max<Tick>(0, view().xToTick(0.0));
    const Tick last = view().xToTick(static_cast<double>(width));

    struct Segment { float x0, x1; const RegionPaint* paint; };
    std::vector<Segment> segments;
    Tick cursor = first;
    auto push = [&](Tick a, Tick b, const RegionPaint* rp) {
        if (b <= a) return;
        const auto x0 = std::max(0.0f, static_cast<float>(view().tickToX(a)));
        const auto x1 = std::min(width, static_cast<float>(view().tickToX(b)));
        if (x1 > x0) segments.push_back({x0, x1, rp});
    };
    for (const auto& r : regions) {  // sorted by time
        if (r.end <= cursor) continue;
        if (r.start >= last) break;
        if (r.start > cursor) push(cursor, r.start, &gapPaint);
        push(std::max(r.start, cursor), r.end, &r);
        cursor = r.end;
    }
    push(cursor, last, &gapPaint);

    const int rows = static_cast<int>(std::ceil(static_cast<float>(getHeight()) / kRowHeight));
    for (int row = 0; row < rows; ++row) {
        const int pitch = lowPitch + row;
        if (pitch > 127) break;
        const int pc = pitch % 12;
        const float y = yOfPitchTop(pitch);
        for (const auto& s : segments) {
            const juce::Rectangle<float> cell(s.x0, y, s.x1 - s.x0, kRowHeight);
            g.setColour(s.paint->background[static_cast<std::size_t>(pc)]);
            g.fillRect(cell);
            if (s.paint->chromatic[static_cast<std::size_t>(pc)]) {
                g.setTiledImageFill(hatch, 0, 0, 1.0f);
                g.fillRect(cell);
            }
        }
        const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
        if (black) {
            g.setColour(juce::Colours::black.withAlpha(0.16f));
            g.fillRect(0.0f, y, width, kRowHeight);
        }
        g.setColour(juce::Colours::black.withAlpha(pc == 0 ? 0.7f : 0.3f));
        g.fillRect(0.0f, y + kRowHeight - 1.0f, width, 1.0f);
        if (pc == 0) {
            g.setColour(ui::mutedTextColour());
            g.setFont(ui::font(9.0f, true));
            g.drawText(pitchName(model.key, pitch), 3, static_cast<int>(y), 28, static_cast<int>(kRowHeight), juce::Justification::centredLeft);
        }
    }

    // chord names along the top, so it is clear which chord a stretch of background belongs to
    g.setFont(ui::font(10.0f, true));
    for (const auto& r : regions) {
        const auto x0 = static_cast<float>(view().tickToX(r.start));
        const auto x1 = static_cast<float>(view().tickToX(r.end));
        if (x1 < 0.0f || x0 > width || x1 - x0 < 30.0f) continue;
        g.setColour(juce::Colours::white.withAlpha(0.55f));
        g.drawText(r.chordName, juce::Rectangle<float>(std::max(x0, 30.0f) + 3.0f, 1.0f, x1 - std::max(x0, 30.0f) - 4.0f, 12.0f), juce::Justification::centredLeft, true);
        // exact chord boundary
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.fillRect(x0 - 0.5f, 0.0f, 1.0f, static_cast<float>(getHeight()));
    }
}

void MelodyRollComponent::drawGridLines(juce::Graphics& g) const {
    const Tick first = std::max<Tick>(0, view().xToTick(-40.0));
    const Tick last = view().xToTick(static_cast<double>(getWidth()) + 40.0);
    for (const auto& m : buildRuler(model.timeSignature, first, last, view().pixelsPerTick, unit())) {
        if (!m.showLine) continue;
        const auto x = static_cast<float>(view().tickToX(m.tick));
        const float alpha = m.level == MarkLevel::Bar ? 0.55f : m.level == MarkLevel::Beat ? 0.26f : 0.10f;
        g.setColour(juce::Colours::white.withAlpha(alpha * 0.6f));
        g.fillRect(x - 0.5f, 0.0f, m.level == MarkLevel::Bar ? 1.5f : 1.0f, static_cast<float>(getHeight()));
    }
}

void MelodyRollComponent::drawNote(juce::Graphics& g, const MelodyNote& n, bool selected, float alpha) const {
    if (n.pitch < lowPitch || yOfPitchTop(n.pitch) < -kRowHeight) return;
    const auto r = noteBounds(n);
    if (r.getRight() < 0.0f || r.getX() > static_cast<float>(getWidth()) || r.getBottom() < 0.0f || r.getY() > static_cast<float>(getHeight())) return;

    const auto& id = resolver.identity(n.pitch);
    const float velocityBrightness = 0.55f + 0.45f * static_cast<float>(n.velocity) / 127.0f;
    const auto base = toColour(id.colour);
    g.setColour(base.withMultipliedBrightness(velocityBrightness).withAlpha(alpha));
    g.fillRoundedRectangle(r, 2.5f);
    g.setColour(juce::Colours::black.withAlpha(0.55f * alpha));
    g.drawRoundedRectangle(r, 2.5f, 1.0f);
    if (selected) {
        g.setColour(juce::Colours::white.withAlpha(alpha));
        g.drawRoundedRectangle(r.expanded(1.0f), 3.0f, 2.0f);
    }
    if (n.locked) {  // small bar along the top edge
        g.setColour(juce::Colours::black.withAlpha(0.8f * alpha));
        g.fillRect(r.getX() + 2.0f, r.getY(), std::min(10.0f, r.getWidth() - 4.0f), 2.0f);
    }

    const auto mode = model.melodySettings.labels;
    if (mode != LabelMode::None && r.getWidth() >= 14.0f) {
        juce::String text;
        const auto deg = juce::String(id.label);
        const auto pitchText = juce::String(noteName(model.key, id.pitchClass));
        if (mode == LabelMode::Degree) text = deg;
        else if (mode == LabelMode::Pitch) text = pitchName(model.key, n.pitch);
        else text = pitchText + " " + deg;
        g.setColour((base.getPerceivedBrightness() > 0.62f ? juce::Colours::black : juce::Colours::white).withAlpha(alpha));
        g.setFont(ui::font(9.0f, true));
        g.drawText(text, r.reduced(2.0f, 0.0f), juce::Justification::centredLeft, false);
    }
}

void MelodyRollComponent::paint(juce::Graphics& g) {
    g.fillAll(ui::controlFillColour().darker(0.4f));
    drawBackground(g);
    drawGridLines(g);

    const bool moving = dragging && gesture != Gesture::Marquee;
    const Progression& shown = moving ? preview : model;
    if (moving && gesture == Gesture::Move) {
        for (auto i : selection) {  // originals stay as faint outlines
            if (i < model.melody.size()) {
                g.setColour(juce::Colours::white.withAlpha(0.4f));
                g.drawRect(noteBounds(model.melody[i]), 1.0f);
            }
        }
    }
    const auto& sel = moving ? previewSelection : selection;
    for (std::size_t i = 0; i < shown.melody.size(); ++i) {
        const bool s = std::find(sel.begin(), sel.end(), i) != sel.end();
        drawNote(g, shown.melody[i], s, moving && s ? 0.8f : 1.0f);
    }

    if (gesture == Gesture::Marquee && dragging) {
        g.setColour(juce::Colours::white.withAlpha(0.12f));
        g.fillRect(marquee);
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.drawRect(marquee, 1.0f);
    }

    // hover ghost: where a click would insert
    if (gesture == Gesture::None && hover.x >= 0.0f && noteAt(model, hover) < 0) {
        const Tick u = std::max<Tick>(unit(), 1);
        const Tick cell = (std::max<Tick>(view().xToTick(hover.x), 0) / u) * u;
        MelodyNote ghost;
        ghost.pitch = pitchAtY(hover.y);
        ghost.startTick = cell;
        ghost.durationTicks = insertLength();
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawRect(noteBounds(ghost), 1.0f);
    }

    const auto head = chordProcessor.getPlayhead();
    if (head.valid) {
        const auto x = static_cast<float>(view().tickToX(static_cast<Tick>(std::llround(head.ppq * kTicksPerQuarter))));
        if (x >= 0.0f && x <= static_cast<float>(getWidth())) {
            g.setColour(juce::Colours::white.withAlpha(head.playing ? 0.95f : 0.45f));
            g.fillRect(x - 1.0f, 0.0f, 2.0f, static_cast<float>(getHeight()));
        }
    }
}

// ------------------------------------------------------------------------------------------ selection

void MelodyRollComponent::setSelectionInternal(std::vector<std::size_t> indices, bool notify) {
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    selection = std::move(indices);
    repaint();
    if (notify && onSelectionChanged) onSelectionChanged();
}

void MelodyRollComponent::selectAll() {
    std::vector<std::size_t> all(model.melody.size());
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = i;
    setSelectionInternal(all, true);
}

int MelodyRollComponent::primaryVelocity() const {
    return selection.empty() || selection.front() >= model.melody.size() ? -1 : model.melody[selection.front()].velocity;
}

bool MelodyRollComponent::primaryLocked() const {
    return !selection.empty() && selection.front() < model.melody.size() && model.melody[selection.front()].locked;
}

// ------------------------------------------------------------------------------------------ mouse

void MelodyRollComponent::mouseMove(const juce::MouseEvent& e) {
    hover = e.position;
    const int hit = noteAt(model, e.position);
    if (hit >= 0) {
        const auto r = noteBounds(model.melody[static_cast<std::size_t>(hit)]);
        const bool edge = r.getWidth() > 16.0f && (e.position.x - r.getX() < kEdgeGrab || r.getRight() - e.position.x < kEdgeGrab);
        setMouseCursor(edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
    } else {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    repaint();
}

void MelodyRollComponent::mouseExit(const juce::MouseEvent&) {
    hover = {-1.0f, -1.0f};
    repaint();
}

void MelodyRollComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (e.mods.isPopupMenu()) return;
    downPosition = e.position;
    dragging = false;
    pressedNote = noteAt(model, e.position);
    gesture = Gesture::None;

    if (pressedNote >= 0) {
        const auto idx = static_cast<std::size_t>(pressedNote);
        const auto& n = model.melody[idx];
        if (e.mods.isShiftDown()) {
            auto next = selection;
            const auto it = std::find(next.begin(), next.end(), idx);
            if (it != next.end()) { next.erase(it); setSelectionInternal(next, true); pressedNote = -1; return; }
            next.push_back(idx);
            setSelectionInternal(next, true);
        } else if (!isSelected(idx)) {
            setSelectionInternal({idx}, true);
        }
        const auto r = noteBounds(n);
        if (r.getWidth() > 16.0f && e.position.x - r.getX() < kEdgeGrab) gesture = Gesture::ResizeStart;
        else if (r.getWidth() > 16.0f && r.getRight() - e.position.x < kEdgeGrab) gesture = Gesture::ResizeEnd;
        else gesture = Gesture::Move;
        grabOffset = view().xToTick(e.position.x) - n.startTick;
        grabPitch = n.pitch;
        lastAuditionedPitch = n.pitch;
        lastDesiredStart = n.startTick;
        lastPitchDelta = 0;
        lastDesiredEdge = n.endTick();
        if (gesture == Gesture::Move) chordProcessor.auditionMelodyNote(n.pitch, n.velocity);
        preview = model;
        previewSelection = selection;
    } else {
        selectionAtMarqueeStart = e.mods.isShiftDown() ? selection : std::vector<std::size_t>{};
        if (!e.mods.isShiftDown()) setSelectionInternal({}, true);
        gesture = Gesture::Marquee;  // a click without a drag inserts a note on mouse-up
    }
}

void MelodyRollComponent::mouseDrag(const juce::MouseEvent& e) {
    if (gesture == Gesture::None) return;
    if (!dragging) {
        if (e.position.getDistanceFrom(downPosition) < kDragThreshold) return;
        dragging = true;
    }
    const Tick u = unit();

    if (gesture == Gesture::Marquee) {
        marquee = juce::Rectangle<float>(downPosition, e.position);
        std::vector<std::size_t> hits = selectionAtMarqueeStart;
        for (std::size_t i = 0; i < model.melody.size(); ++i) {
            if (noteBounds(model.melody[i]).intersects(marquee)) hits.push_back(i);
        }
        std::sort(hits.begin(), hits.end());
        hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
        if (hits != selection) setSelectionInternal(hits, true);
        repaint();
        return;
    }
    if (pressedNote < 0 || static_cast<std::size_t>(pressedNote) >= model.melody.size()) return;
    const auto anchor = static_cast<std::size_t>(pressedNote);
    Progression trial = model;
    OpResult r;
    if (gesture == Gesture::Move) {
        const Tick desired = view().xToTick(e.position.x) - grabOffset;
        const int delta = pitchAtY(e.position.y) - grabPitch;
        r = moveMelodyNotes(trial, selection, anchor, desired, delta, u);
        if (r.ok) {
            lastDesiredStart = desired;
            lastPitchDelta = delta;
            const int shownPitch = model.melody[anchor].pitch + std::clamp(delta, -model.melody[anchor].pitch, 127 - model.melody[anchor].pitch);
            if (shownPitch != lastAuditionedPitch) {
                lastAuditionedPitch = shownPitch;
                chordProcessor.auditionMelodyNote(shownPitch, model.melody[anchor].velocity);
            }
        }
    } else if (gesture == Gesture::ResizeEnd) {
        lastDesiredEdge = view().xToTick(e.position.x);
        r = resizeMelodyNoteEnd(trial, anchor, lastDesiredEdge, u);
    } else {
        lastDesiredEdge = view().xToTick(e.position.x);
        r = resizeMelodyNoteStart(trial, anchor, lastDesiredEdge, u);
    }
    if (r.ok) {
        preview = std::move(trial);
        previewSelection = gesture == Gesture::Move ? r.indices : (r.indices.empty() ? selection : r.indices);
    }
    repaint();
}

void MelodyRollComponent::commitGesture() {
    if (pressedNote < 0 || preview.melody == model.melody) return;
    const auto anchor = static_cast<std::size_t>(pressedNote);
    const auto sel = selection;
    const Tick u = unit();
    const Gesture g = gesture;
    const Tick desired = lastDesiredStart, edge = lastDesiredEdge;
    const int delta = lastPitchDelta;
    std::vector<std::size_t> newSel;
    const auto result = chordProcessor.editProgression(
        g == Gesture::Move ? (sel.size() > 1 ? "Move notes" : "Move note") : "Resize note", [&](Progression& p) {
            OpResult r;
            if (g == Gesture::Move) r = moveMelodyNotes(p, sel, anchor, desired, delta, u);
            else if (g == Gesture::ResizeEnd) r = resizeMelodyNoteEnd(p, anchor, edge, u);
            else r = resizeMelodyNoteStart(p, anchor, edge, u);
            if (!r.ok) throw std::runtime_error(r.error);
            newSel = r.indices;
        });
    model = chordProcessor.getProgression();
    if (result.ok) {
        selection = g == Gesture::Move ? newSel : (newSel.empty() ? sel : newSel);
    } else {
        report("Cannot do that: " + juce::String(result.error));
    }
}

void MelodyRollComponent::mouseUp(const juce::MouseEvent& e) {
    const Gesture g = gesture;
    const bool wasDragging = dragging;
    if (g == Gesture::Move || g == Gesture::ResizeStart || g == Gesture::ResizeEnd) {
        if (wasDragging) commitGesture();
        if (onSelectionChanged) onSelectionChanged();
    } else if (g == Gesture::Marquee && !wasDragging && !e.mods.isShiftDown()) {
        const Tick u = std::max<Tick>(unit(), 1);
        const Tick cell = (std::max<Tick>(view().xToTick(e.position.x), 0) / u) * u;
        const int pitch = pitchAtY(e.position.y);
        const Tick len = insertLength();
        std::vector<std::size_t> added;
        const auto result = chordProcessor.editProgression("Add note", [&](Progression& p) {
            const auto r = addMelodyNote(p, pitch, cell, u, len);
            if (!r.ok) throw std::runtime_error(r.error);
            added = r.indices;
        });
        model = chordProcessor.getProgression();
        if (result.ok) {
            setSelectionInternal(added, true);
            chordProcessor.auditionMelodyNote(pitch, 90);
        } else {
            report("Not added: " + juce::String(result.error));
        }
    }
    gesture = Gesture::None;
    dragging = false;
    pressedNote = -1;
    repaint();
}

void MelodyRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    if (gesture != Gesture::None) return;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown() || e.mods.isShiftDown()) {
        grid.mouseWheelMove(e.getEventRelativeTo(&grid), w);  // time zoom / scroll is shared with the chord grid
        return;
    }
    const float delta = std::abs(w.deltaY) > 1e-6f ? w.deltaY : w.deltaX;
    lowPitch += static_cast<int>(std::lround(static_cast<double>(delta) * 12.0));
    pitchPlaced = true;
    clampPitchScroll();
    repaint();
}

// ------------------------------------------------------------------------------------------ commands

void MelodyRollComponent::editSelection(const std::string& label,
                                        const std::function<OpResult(Progression&, const std::vector<std::size_t>&)>& op, bool selectResult) {
    if (selection.empty()) return;
    const auto sel = selection;
    std::vector<std::size_t> out;
    const auto result = chordProcessor.editProgression(label, [&](Progression& p) {
        const auto r = op(p, sel);
        if (!r.ok) throw std::runtime_error(r.error);
        out = r.indices;
    });
    model = chordProcessor.getProgression();
    if (!result.ok) { report("Cannot do that: " + juce::String(result.error)); return; }
    setSelectionInternal(selectResult ? out : std::vector<std::size_t>{}, true);
}

void MelodyRollComponent::deleteSelection() {
    editSelection(selection.size() > 1 ? "Delete notes" : "Delete note", [](Progression& p, const std::vector<std::size_t>& s) { return deleteMelodyNotes(p, s); }, false);
}

void MelodyRollComponent::duplicateSelection() {
    editSelection(selection.size() > 1 ? "Duplicate notes" : "Duplicate note", [](Progression& p, const std::vector<std::size_t>& s) { return duplicateMelodyNotes(p, s); }, true);
}

void MelodyRollComponent::copySelection() {
    clipboard = melodyClipboard(model, selection);
    if (!clipboard.empty()) report("Copied " + juce::String(static_cast<int>(clipboard.size())) + " note(s)");
}

void MelodyRollComponent::pasteAt(Tick tick) {
    if (clipboard.empty()) { report("Nothing copied yet: select notes and press Ctrl+C"); return; }
    const auto clip = clipboard;
    const Tick u = unit();
    std::vector<std::size_t> out;
    const auto result = chordProcessor.editProgression("Paste notes", [&](Progression& p) {
        const auto r = pasteMelodyNotes(p, clip, tick, u);
        if (!r.ok) throw std::runtime_error(r.error);
        out = r.indices;
    });
    model = chordProcessor.getProgression();
    if (result.ok) setSelectionInternal(out, true);
    else report("Cannot paste: " + juce::String(result.error));
}

void MelodyRollComponent::setSelectedVelocity(int velocity) {
    if (selection.empty()) return;
    const auto sel = selection;
    const int v = std::clamp(velocity, 1, 127);
    (void)chordProcessor.editProgression(
        "Velocity", [&](Progression& p) {
            const auto r = setMelodyVelocity(p, sel, v);
            if (!r.ok) throw std::runtime_error(r.error);
        },
        "melodyVelocity");
    model = chordProcessor.getProgression();
    repaint();
}

void MelodyRollComponent::toggleLockSelected() {
    if (selection.empty()) return;
    bool allLocked = true;
    for (auto i : selection) allLocked = allLocked && i < model.melody.size() && model.melody[i].locked;
    editSelection(allLocked ? "Unlock notes" : "Lock notes",
                  [allLocked](Progression& p, const std::vector<std::size_t>& s) { return setMelodyLocked(p, s, !allLocked); }, true);
}

bool MelodyRollComponent::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    if (key == juce::KeyPress::escapeKey) { setSelectionInternal({}, true); return true; }
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (mods.isCommandDown()) {
        if (code == 'D' || code == 'd') { duplicateSelection(); return true; }
        if (code == 'A' || code == 'a') { selectAll(); return true; }
        if (code == 'C' || code == 'c') { copySelection(); return true; }
        if (code == 'V' || code == 'v') {
            const Tick at = hover.x >= 0.0f ? view().xToTick(hover.x) : 0;
            pasteAt(at);
            return true;
        }
        return false;
    }
    if (selection.empty()) return false;
    int pitchDelta = 0;
    Tick timeDelta = 0;
    if (key == juce::KeyPress::upKey) pitchDelta = mods.isShiftDown() ? 12 : 1;
    else if (key == juce::KeyPress::downKey) pitchDelta = mods.isShiftDown() ? -12 : -1;
    else if (key == juce::KeyPress::rightKey) timeDelta = std::max<Tick>(unit(), 1);
    else if (key == juce::KeyPress::leftKey) timeDelta = -std::max<Tick>(unit(), 1);
    else return false;
    const auto sel = selection;
    const auto anchor = sel.front();
    if (anchor >= model.melody.size()) return true;
    const Tick target = model.melody[anchor].startTick + timeDelta;
    editSelection(pitchDelta != 0 ? "Transpose notes" : "Nudge notes",
                  [=](Progression& p, const std::vector<std::size_t>& s) { return moveMelodyNotes(p, s, anchor, target, pitchDelta, 1); }, true);
    if (pitchDelta != 0 && !selection.empty() && selection.front() < model.melody.size()) {
        chordProcessor.auditionMelodyNote(model.melody[selection.front()].pitch, model.melody[selection.front()].velocity);
    }
    return true;
}
