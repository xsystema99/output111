#pragma once

#include "UiKit.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <chordcore/MidiFile.h>
#include <chordcore/MidiFilename.h>
#include <chordcore/Progression.h>

#include <functional>

/// The "Export MIDI" panel: bar range, what to export, melody channel, chord voicing, and the Filename Format selector
/// (Absolute | Roman | Both) with the proposed file name shown live beneath it. The name comes from the one shared
/// generator (chordcore::generateMidiFilename), so the preview is exactly what the file chooser proposes.
class ExportDialog final : public juce::Component {
public:
    ExportDialog(const chordcore::Progression& model, int firstBar, int lastBar, int totalBars);

    /// Called with the chosen options and filename format when the user presses "Choose file...". Return false to keep the dialog open.
    std::function<bool(const chordcore::ProjectExportOptions&, chordcore::MidiFilenameFormat)> onChoose;
    std::function<void()> onCancel;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void setFormat(chordcore::MidiFilenameFormat);
    chordcore::ProjectExportOptions currentOptions() const;  // from + to may be invalid: the bars are only clamped for the preview
    void updatePreview();

    chordcore::Progression model;
    chordcore::MidiFilenameFormat format;
    int totalBars;

    juce::Label heading, fromCaption{{}, "From bar"}, toCaption{{}, "To bar"}, partCaption{{}, "Export"}, channelCaption{{}, "Melody channel"},
        voicingCaption{{}, "Chord voicing"}, formatCaption{{}, "Filename Format"}, previewCaption{{}, "File name"}, preview;
    juce::TextEditor fromEditor, toEditor;
    juce::ComboBox partBox, channelBox, voicingBox;
    ui::StyledButton absoluteButton{"Absolute", ui::StyledButton::Look::Pill, 12.0f}, romanButton{"Roman", ui::StyledButton::Look::Pill, 12.0f},
        bothButton{"Both", ui::StyledButton::Look::Pill, 12.0f}, chooseButton{"Choose file...", ui::StyledButton::Look::Primary, 14.0f},
        cancelButton{"Cancel", ui::StyledButton::Look::Outline, 14.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ExportDialog)
};
