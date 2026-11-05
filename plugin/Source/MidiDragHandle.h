#pragma once

#include "UiKit.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

/// A small "grab me" pill. Dragging it out of the plugin window starts an operating-system file drag of a
/// freshly written .mid file, so it can be dropped onto a track or clip slot of the host (or Explorer).
/// The bytes come from `makeFile`, which must use the same export path as the Export button.
class MidiDragHandle final : public juce::Component {
public:
    /// Writes the current progression to a .mid file and returns it (an invalid File = failure, with a reason in `error`).
    std::function<juce::File(juce::String& error)> makeFile;
    std::function<void(const juce::String&)> onMessage;

    MidiDragHandle() { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }

    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(ui::controlFillColour());
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(ui::controlOutlineColour());
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, 1.0f);
        g.setColour(ui::textColour());
        g.setFont(ui::font(13.0f, true));
        g.drawText(juce::String(juce::CharPointer_UTF8("\xE2\x87\xA9  Drag MIDI")), getLocalBounds(), juce::Justification::centred);
    }

    void mouseUp(const juce::MouseEvent& e) override {
        if (!started && e.getDistanceFromDragStart() < 4 && onMessage)
            onMessage("Drag this onto a track or clip slot in your DAW (or into a folder) to drop the progression as a .mid file.");
        started = false;
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 6 || !makeFile) return;
        started = true;
        juce::String error;
        const auto file = makeFile(error);
        if (file == juce::File()) {
            if (onMessage) onMessage("Cannot drag MIDI: " + error);
            return;
        }
        juce::Component::SafePointer<MidiDragHandle> self(this);
        juce::DragAndDropContainer::performExternalDragDropOfFiles({file.getFullPathName()}, /*canMoveFiles*/ false, this, [self] {
            if (self != nullptr && self->onMessage) self->onMessage("Drag finished.");
        });
    }

private:
    bool started = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiDragHandle)
};
