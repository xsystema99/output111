#include "PluginEditor.h"

#include <chordcore/NoteEngine.h>
#include <chordcore/ProgressionText.h>

#include <exception>

namespace {
const char* const kTonicNames[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
}

ChordSketchEditor::ChordSketchEditor(ChordSketchProcessor& p) : juce::AudioProcessorEditor(&p), processor(p) {
    addAndMakeVisible(chordsLabel);
    chordsEditor.setMultiLine(false);
    chordsEditor.setReturnKeyStartsNewLine(false);
    chordsEditor.setTextToShowWhenEmpty("e.g.  I V vi IV   (rest: -   length in bars: I:2)", juce::Colours::grey);
    chordsEditor.onReturnKey = [this] { applyChordsClicked(); };
    addAndMakeVisible(chordsEditor);

    applyButton.onClick = [this] { applyChordsClicked(); };
    addAndMakeVisible(applyButton);

    addAndMakeVisible(keyLabel);
    for (int i = 0; i < 12; ++i) tonicBox.addItem(kTonicNames[i], i + 1);
    modeBox.addItem("major", 1);
    modeBox.addItem("minor", 2);
    tonicBox.onChange = [this] { keyChanged(); };
    modeBox.onChange = [this] { keyChanged(); };
    addAndMakeVisible(tonicBox);
    addAndMakeVisible(modeBox);

    addAndMakeVisible(tempoLabel);
    tempoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 22);
    tempoSlider.setRange(20.0, 400.0, 0.1);
    tempoSlider.onValueChange = [this] { tempoChanged(); };
    addAndMakeVisible(tempoSlider);

    undoButton.onClick = [this] { processor.undo(); };
    redoButton.onClick = [this] { processor.redo(); };
    exportButton.onClick = [this] { exportMidiClicked(); };
    addAndMakeVisible(undoButton);
    addAndMakeVisible(redoButton);
    addAndMakeVisible(exportButton);

    summaryLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(summaryLabel);
    addAndMakeVisible(statusLabel);

    setSize(560, 270);
    refreshFromModel();
    processor.changes().addChangeListener(this);
}

ChordSketchEditor::~ChordSketchEditor() { processor.changes().removeChangeListener(this); }

void ChordSketchEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ChordSketchEditor::resized() {
    auto area = getLocalBounds().reduced(16);

    auto chordsRow = area.removeFromTop(28);
    chordsLabel.setBounds(chordsRow.removeFromLeft(56));
    applyButton.setBounds(chordsRow.removeFromRight(72));
    chordsRow.removeFromRight(8);
    chordsEditor.setBounds(chordsRow);
    area.removeFromTop(10);

    auto keyRow = area.removeFromTop(28);
    keyLabel.setBounds(keyRow.removeFromLeft(56));
    tonicBox.setBounds(keyRow.removeFromLeft(80));
    keyRow.removeFromLeft(8);
    modeBox.setBounds(keyRow.removeFromLeft(100));
    area.removeFromTop(10);

    auto tempoRow = area.removeFromTop(28);
    tempoLabel.setBounds(tempoRow.removeFromLeft(56));
    tempoSlider.setBounds(tempoRow);
    area.removeFromTop(12);

    auto buttons = area.removeFromTop(32);
    undoButton.setBounds(buttons.removeFromLeft(170));
    buttons.removeFromLeft(8);
    redoButton.setBounds(buttons.removeFromLeft(170));
    exportButton.setBounds(buttons.removeFromRight(130));
    area.removeFromTop(10);

    summaryLabel.setBounds(area.removeFromTop(24));
    statusLabel.setBounds(area);
}

void ChordSketchEditor::setStatus(const juce::String& text) { statusLabel.setText(text, juce::dontSendNotification); }

void ChordSketchEditor::changeListenerCallback(juce::ChangeBroadcaster*) { refreshFromModel(); }

void ChordSketchEditor::refreshFromModel() {
    const auto prog = processor.getProgression();
    const auto history = processor.getHistoryInfo();

    const auto text = juce::String(chordcore::formatProgressionText(prog));
    if (chordsEditor.getText() != text) chordsEditor.setText(text, false);

    tonicBox.setSelectedId(prog.key.tonic + 1, juce::dontSendNotification);
    modeBox.setSelectedId(prog.key.mode == chordcore::Mode::Major ? 1 : 2, juce::dontSendNotification);
    tempoSlider.setValue(prog.tempoBpm, juce::dontSendNotification);

    undoButton.setEnabled(history.canUndo);
    redoButton.setEnabled(history.canRedo);
    undoButton.setButtonText(history.canUndo ? "Undo: " + juce::String(history.undoLabel) : juce::String("Undo"));
    redoButton.setButtonText(history.canRedo ? "Redo: " + juce::String(history.redoLabel) : juce::String("Redo"));

    juce::String summary;
    try {
        const auto noteCount = chordcore::generateNotes(prog).size();
        summary << (int) prog.blocks.size() << " chords, " << (int) noteCount << " notes, "
                << juce::String(prog.tempoBpm, 1) << " BPM, " << prog.timeSignature.numerator << "/"
                << prog.timeSignature.denominator;
    } catch (const std::exception& e) {
        summary = juce::String("Invalid progression: ") + e.what();
    }
    summaryLabel.setText(summary, juce::dontSendNotification);

    const auto loadError = processor.getLastLoadError();
    if (!loadError.empty()) setStatus("Could not restore the saved project (kept the current one): " + juce::String(loadError));
}

void ChordSketchEditor::applyChordsClicked() {
    const auto current = processor.getProgression();
    const auto parsed = chordcore::parseProgressionText(chordsEditor.getText().toStdString(), current.timeSignature);
    if (!parsed.ok) {
        setStatus("Cannot apply: " + juce::String(parsed.error));
        return;
    }
    const auto result = processor.editProgression("Edit chords", [&parsed](chordcore::Progression& p) { p.blocks = parsed.blocks; });
    setStatus(result.ok ? juce::String("Applied.") : "Cannot apply: " + juce::String(result.error));
}

void ChordSketchEditor::keyChanged() {
    const int tonic = tonicBox.getSelectedId() - 1;
    const auto mode = modeBox.getSelectedId() == 2 ? chordcore::Mode::Minor : chordcore::Mode::Major;
    if (tonic < 0) return;
    const auto result = processor.editProgression("Change key", [tonic, mode](chordcore::Progression& p) {
        p.key.tonic = tonic;
        p.key.mode = mode;
    });
    if (!result.ok) setStatus("Cannot change key: " + juce::String(result.error));
}

void ChordSketchEditor::tempoChanged() {
    const double bpm = tempoSlider.getValue();
    const auto result = processor.editProgression("Change tempo", [bpm](chordcore::Progression& p) { p.tempoBpm = bpm; }, "tempo");
    if (!result.ok) setStatus("Cannot change tempo: " + juce::String(result.error));
}

void ChordSketchEditor::exportMidiClicked() {
    const auto defaultFile =
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ChordSketch.mid");

    fileChooser = std::make_unique<juce::FileChooser>("Export MIDI file", defaultFile, "*.mid");
    const auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting;

    // SafePointer: the host may close the editor while the dialog is open.
    juce::Component::SafePointer<ChordSketchEditor> safeThis(this);
    fileChooser->launchAsync(chooserFlags, [safeThis](const juce::FileChooser& chooser) {
        if (safeThis == nullptr) return;
        auto file = chooser.getResult();
        if (file == juce::File()) return;  // cancelled
        if (!file.hasFileExtension("mid")) file = file.withFileExtension("mid");
        safeThis->writeMidiTo(file);
    });
}

void ChordSketchEditor::writeMidiTo(const juce::File& file) {
    try {
        const auto bytes = processor.exportMidiBytes();
        if (file.replaceWithData(bytes.data(), bytes.size())) {
            setStatus("Wrote " + juce::String((int) bytes.size()) + " bytes to " + file.getFullPathName());
        } else {
            setStatus("Could not write " + file.getFullPathName());
        }
    } catch (const std::exception& e) {
        setStatus(juce::String("Export failed: ") + e.what());
    }
}
