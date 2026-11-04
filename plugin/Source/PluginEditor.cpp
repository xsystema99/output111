#include "PluginEditor.h"

#include <chordcore/NoteEngine.h>

#include <exception>

ChordSketchEditor::ChordSketchEditor(ChordSketchProcessor& p) : juce::AudioProcessorEditor(&p), processor(p) {
    const auto& prog = processor.getProgression();

    juce::String summary;
    try {
        const auto noteCount = chordcore::generateNotes(prog).size();
        summary << (int) prog.blocks.size() << " chords, " << (int) noteCount << " notes, "
                << juce::String(prog.tempoBpm, 0) << " BPM, " << prog.timeSignature.numerator << "/"
                << prog.timeSignature.denominator;
    } catch (const std::exception& e) {
        summary = juce::String("Invalid progression: ") + e.what();
    }

    summaryLabel.setText(summary, juce::dontSendNotification);
    summaryLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(summaryLabel);

    statusLabel.setText("Demo progression (C major, I - V - vi - IV).\nPlays from bar 1, in sync with the transport.", juce::dontSendNotification);
    statusLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(statusLabel);

    exportButton.onClick = [this] { exportMidiClicked(); };
    addAndMakeVisible(exportButton);

    setSize(420, 180);
}

void ChordSketchEditor::paint(juce::Graphics& g) {
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void ChordSketchEditor::resized() {
    auto area = getLocalBounds().reduced(16);
    summaryLabel.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    exportButton.setBounds(area.removeFromTop(36).withSizeKeepingCentre(160, 36));
    area.removeFromTop(8);
    statusLabel.setBounds(area.removeFromTop(48));
}

void ChordSketchEditor::exportMidiClicked() {
    const auto defaultFile =
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("ChordSketch-demo.mid");

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
            statusLabel.setText("Wrote " + juce::String((int) bytes.size()) + " bytes to\n" + file.getFullPathName(),
                                juce::dontSendNotification);
        } else {
            statusLabel.setText("Could not write " + file.getFullPathName(), juce::dontSendNotification);
        }
    } catch (const std::exception& e) {
        statusLabel.setText(juce::String("Export failed: ") + e.what(), juce::dontSendNotification);
    }
}
