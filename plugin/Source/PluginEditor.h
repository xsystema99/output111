#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

/// Milestone 4 editor: enough controls to edit and persist a project before the timeline UI (milestone 6).
///
///   Chords   one line of Roman numerals, e.g.  "I V vi:2 - IV7"  (Enter or Apply)
///   Key      tonic and mode
///   Tempo    used for export and as the fallback when the host gives none (coalesced into one undo step)
///   Undo/Redo, Export MIDI...
class ChordSketchEditor final : public juce::AudioProcessorEditor, private juce::ChangeListener {
public:
    explicit ChordSketchEditor(ChordSketchProcessor&);
    ~ChordSketchEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshFromModel();
    void applyChordsClicked();
    void keyChanged();
    void tempoChanged();
    void exportMidiClicked();
    void writeMidiTo(const juce::File& file);
    void setStatus(const juce::String& text);

    ChordSketchProcessor& processor;
    juce::Label chordsLabel{{}, "Chords"};
    juce::TextEditor chordsEditor;
    juce::TextButton applyButton{"Apply"};
    juce::Label keyLabel{{}, "Key"};
    juce::ComboBox tonicBox;
    juce::ComboBox modeBox;
    juce::Label tempoLabel{{}, "Tempo"};
    juce::Slider tempoSlider;
    juce::TextButton undoButton{"Undo"};
    juce::TextButton redoButton{"Redo"};
    juce::TextButton exportButton{"Export MIDI..."};
    juce::Label summaryLabel;
    juce::Label statusLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;  // must outlive the async dialog

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordSketchEditor)
};
