#include "ExportDialog.h"

#include <algorithm>

using namespace chordcore;

namespace {

void styleLabel(juce::Label& label, float height, bool bold, juce::Colour colour) {
    label.setFont(ui::font(height, bold));
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(juce::Justification::centredLeft);
    label.setInterceptsMouseClicks(false, false);
}

void styleCombo(juce::ComboBox& box) {
    box.setColour(juce::ComboBox::backgroundColourId, ui::controlFillColour());
    box.setColour(juce::ComboBox::outlineColourId, ui::controlOutlineColour());
    box.setColour(juce::ComboBox::textColourId, ui::textColour());
    box.setColour(juce::ComboBox::arrowColourId, ui::mutedTextColour());
}

void styleEditor(juce::TextEditor& e) {
    e.setInputRestrictions(3, "0123456789");
    e.setColour(juce::TextEditor::backgroundColourId, ui::controlFillColour());
    e.setColour(juce::TextEditor::textColourId, ui::textColour());
    e.setColour(juce::TextEditor::outlineColourId, ui::controlOutlineColour());
    e.setFont(ui::font(14.0f));
}

}  // namespace

ExportDialog::ExportDialog(const Progression& m, int firstBar, int lastBar, int bars) : model(m), format(m.exportNaming), totalBars(bars) {
    styleLabel(heading, 15.0f, true, ui::textColour());
    heading.setText("Bars to export (the whole project is bars 1 to " + juce::String(totalBars) + ")", juce::dontSendNotification);
    for (auto* l : {&fromCaption, &toCaption, &partCaption, &channelCaption, &voicingCaption, &previewCaption}) styleLabel(*l, 12.0f, false, ui::mutedTextColour());
    styleLabel(formatCaption, 13.0f, true, ui::textColour());
    styleLabel(preview, 13.0f, false, ui::textColour());
    preview.setMinimumHorizontalScale(1.0f);  // never squeeze: long names end with an ellipsis, the tooltip shows the whole name
    for (auto* l : {&heading, &fromCaption, &toCaption, &partCaption, &channelCaption, &voicingCaption, &formatCaption, &previewCaption, &preview}) addAndMakeVisible(*l);

    styleEditor(fromEditor);
    styleEditor(toEditor);
    fromEditor.setText(juce::String(firstBar), false);
    toEditor.setText(juce::String(lastBar), false);
    addAndMakeVisible(fromEditor);
    addAndMakeVisible(toEditor);

    partBox.addItem("Chords + melody (Combined)", 1);
    partBox.addItem("Chords only", 2);
    partBox.addItem("Melody only", 3);
    channelBox.addItem("Melody on its own channel (" + juce::String(model.melodySettings.melodyChannel) + ")", 1);
    channelBox.addItem("Melody on channel 1 (compatibility)", 2);
    voicingBox.addItem("Chords as played (voiced, with Chord Density)", 1);
    voicingBox.addItem("Chords with original voicing", 2);
    for (auto* b : {&partBox, &channelBox, &voicingBox}) {
        styleCombo(*b);
        b->setSelectedId(1, juce::dontSendNotification);
        b->onChange = [this] { updatePreview(); };
        addAndMakeVisible(*b);
    }
    fromEditor.onTextChange = [this] { updatePreview(); };
    toEditor.onTextChange = [this] { updatePreview(); };

    absoluteButton.onClick = [this] { setFormat(MidiFilenameFormat::Absolute); };
    romanButton.onClick = [this] { setFormat(MidiFilenameFormat::Roman); };
    bothButton.onClick = [this] { setFormat(MidiFilenameFormat::Both); };
    absoluteButton.setTooltip("Absolute chord names: Fm9 - Dbmaj7 - Abmaj9 - Eb7");
    romanButton.setTooltip("Chromatic Roman numerals relative to the project tonic: i9 - bVImaj7 - bIIImaj9 - bVII7");
    bothButton.setTooltip("Each chord with its numeral: Fm9 (i9) - Dbmaj7 (bVImaj7) - ...");
    chooseButton.onClick = [this] {
        if (!onChoose) return;
        if (onChoose(currentOptions(), format)) {
            if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState(1);
        }
    };
    cancelButton.onClick = [this] {
        if (onCancel) onCancel();
        if (auto* window = findParentComponentOfClass<juce::DialogWindow>()) window->exitModalState(0);
    };
    for (auto* b : {&absoluteButton, &romanButton, &bothButton, &chooseButton, &cancelButton}) {
        b->setWantsKeyboardFocus(false);
        addAndMakeVisible(*b);
    }
    setFormat(format);
    setSize(460, 420);
}

ProjectExportOptions ExportDialog::currentOptions() const {
    ProjectExportOptions o;
    const int part = partBox.getSelectedId();
    o.part = part == 2 ? ExportPart::Chords : part == 3 ? ExportPart::Melody : ExportPart::Combined;
    o.melodyOnChannel1 = channelBox.getSelectedId() == 2;
    o.originalVoicing = voicingBox.getSelectedId() == 2;
    o.firstBar = fromEditor.getText().getIntValue();
    o.lastBar = toEditor.getText().getIntValue();
    return o;
}

void ExportDialog::setFormat(MidiFilenameFormat f) {
    format = f;
    absoluteButton.setToggleState(f == MidiFilenameFormat::Absolute, juce::dontSendNotification);
    romanButton.setToggleState(f == MidiFilenameFormat::Roman, juce::dontSendNotification);
    bothButton.setToggleState(f == MidiFilenameFormat::Both, juce::dontSendNotification);
    updatePreview();
}

void ExportDialog::updatePreview() {
    auto o = currentOptions();
    const bool valid = o.firstBar >= 1 && o.lastBar >= o.firstBar && o.lastBar <= kMaxLengthBars;
    channelBox.setEnabled(o.part != ExportPart::Chords);
    voicingBox.setEnabled(o.part != ExportPart::Melody);
    chooseButton.setEnabled(valid);
    if (!valid) {
        preview.setText("Bar range not valid: first bar 1 or more, not after the last bar.", juce::dontSendNotification);
        preview.setTooltip({});
        return;
    }
    const juce::String text = juce::String::fromUTF8(generateMidiFilename(model, format, o).c_str());
    preview.setText(text, juce::dontSendNotification);
    preview.setTooltip(text);  // the complete name, however long
}

void ExportDialog::paint(juce::Graphics& g) { g.fillAll(ui::backgroundColour()); }

void ExportDialog::resized() {
    auto area = getLocalBounds().reduced(20, 16);
    heading.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);
    auto row = area.removeFromTop(16);
    fromCaption.setBounds(row.removeFromLeft(100));
    toCaption.setBounds(row.removeFromLeft(100));
    row = area.removeFromTop(28);
    fromEditor.setBounds(row.removeFromLeft(92));
    row.removeFromLeft(8);
    toEditor.setBounds(row.removeFromLeft(92));
    area.removeFromTop(10);
    partCaption.setBounds(area.removeFromTop(16));
    partBox.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    channelCaption.setBounds(area.removeFromTop(16));
    channelBox.setBounds(area.removeFromTop(28));
    area.removeFromTop(8);
    voicingCaption.setBounds(area.removeFromTop(16));
    voicingBox.setBounds(area.removeFromTop(28));
    area.removeFromTop(14);
    formatCaption.setBounds(area.removeFromTop(20));
    row = area.removeFromTop(28);  // the segmented selector
    const int segment = row.getWidth() / 3;
    absoluteButton.setBounds(row.removeFromLeft(segment).reduced(2, 0));
    romanButton.setBounds(row.removeFromLeft(segment).reduced(2, 0));
    bothButton.setBounds(row.reduced(2, 0));
    area.removeFromTop(6);
    previewCaption.setBounds(area.removeFromTop(16));
    preview.setBounds(area.removeFromTop(24));
    area.removeFromTop(10);
    row = area.removeFromBottom(34);
    chooseButton.setBounds(row.removeFromRight(140));
    row.removeFromRight(8);
    cancelButton.setBounds(row.removeFromRight(90));
}
