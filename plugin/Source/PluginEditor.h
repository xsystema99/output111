#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

/// Milestone 1 editor: a summary line, an "Export MIDI..." button and a status line.
/// The timeline UI is milestone 6; this is deliberately minimal.
class ChordSketchEditor final : public juce::AudioProcessorEditor {
public:
    explicit ChordSketchEditor(ChordSketchProcessor&);
    ~ChordSketchEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void exportMidiClicked();
    void writeMidiTo(const juce::File& file);

    ChordSketchProcessor& processor;
    juce::Label summaryLabel;
    juce::Label statusLabel;
    juce::TextButton exportButton{"Export MIDI..."};
    std::unique_ptr<juce::FileChooser> fileChooser;  // must outlive the async dialog

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordSketchEditor)
};
