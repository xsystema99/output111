#include "PluginEditor.h"

#include <chordcore/ChordEdit.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/ProgressionText.h>
#include <chordcore/RomanNumeral.h>
#include <chordcore/Timeline.h>

#include <algorithm>
#include <exception>
#include <stdexcept>

using namespace chordcore;

namespace {

void styleLabel(juce::Label& label, float height, bool bold, juce::Colour colour,
                juce::Justification justification = juce::Justification::centredLeft) {
    label.setFont(ui::font(height, bold));
    label.setColour(juce::Label::textColourId, colour);
    label.setJustificationType(justification);
    label.setInterceptsMouseClicks(false, false);
}

void styleCombo(juce::ComboBox& box) {
    box.setColour(juce::ComboBox::backgroundColourId, ui::controlFillColour());
    box.setColour(juce::ComboBox::outlineColourId, ui::controlOutlineColour());
    box.setColour(juce::ComboBox::textColourId, ui::textColour());
    box.setColour(juce::ComboBox::arrowColourId, ui::mutedTextColour());
    box.setColour(juce::ComboBox::focusedOutlineColourId, ui::mutedTextColour());
}

constexpr int kPaletteDegrees = 7;

}  // namespace

ChordSketchEditor::ChordSketchEditor(ChordSketchProcessor& p)
    : juce::AudioProcessorEditor(&p), chordProcessor(p), model(p.getProgression()), timeline(p) {
    selected = model.blocks.empty() ? -1 : 0;

    // header
    titleLabel.setText("ChordSketch", juce::dontSendNotification);
    styleLabel(titleLabel, 20.0f, true, ui::textColour());
    subtitleLabel.setText("Chord sequencer", juce::dontSendNotification);
    styleLabel(subtitleLabel, 12.0f, false, ui::mutedTextColour());
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);

    // key / scale / meter
    for (auto* caption : {&keyCaption, &scaleCaption, &meterCaption}) {
        styleLabel(*caption, 14.0f, true, ui::textColour());
        addAndMakeVisible(*caption);
    }
    for (int i = 0; i < 12; ++i) tonicBox.addItem("C", i + 1);  // names are set in refreshFromModel (they depend on the scale)
    scaleBox.addItem("Major", 1);
    scaleBox.addItem("Minor", 2);
    styleCombo(tonicBox);
    styleCombo(scaleBox);
    tonicBox.onChange = [this] { keyChanged(); };
    scaleBox.onChange = [this] { scaleChanged(); };
    addAndMakeVisible(tonicBox);
    addAndMakeVisible(scaleBox);
    styleLabel(meterValue, 17.0f, true, ui::textColour(), juce::Justification::centredLeft);
    addAndMakeVisible(meterValue);

    // timeline
    styleLabel(timelineCaption, 14.0f, true, ui::textColour());
    styleLabel(timelineHint, 12.0f, false, ui::mutedTextColour(), juce::Justification::centredRight);
    addAndMakeVisible(timelineCaption);
    addAndMakeVisible(timelineHint);
    timeline.onSelectionChanged = [this](int index) { selectionChanged(index); };
    timeline.onMessage = [this](const juce::String& text) { setStatus(text); };
    addAndMakeVisible(timeline);

    // palette
    styleLabel(paletteCaption, 14.0f, true, ui::textColour());
    addAndMakeVisible(paletteCaption);
    for (int degree = 1; degree <= kPaletteDegrees; ++degree) {
        auto button = std::make_unique<ui::StyledButton>("", ui::StyledButton::Look::Palette, 16.0f);
        button->setPaletteColour(ui::degreeColour(degree));
        button->onClick = [this, degree] { paletteClicked(degree); };
        addAndMakeVisible(*button);
        paletteButtons.push_back(std::move(button));
    }

    // selected chord
    styleLabel(selectedCaption, 14.0f, true, ui::textColour());
    styleLabel(selectedName, 15.0f, true, ui::mutedTextColour(), juce::Justification::centredRight);
    addAndMakeVisible(selectedCaption);
    addAndMakeVisible(selectedName);

    triadButton.onClick = [this] {
        applyToSelected("Triad", [](Progression& prog, std::size_t i) { prog.blocks.at(i).chord = asTriad(prog.blocks.at(i).chord); });
    };
    seventhButton.onClick = [this] {
        applyToSelected("Toggle 7th", [](Progression& prog, std::size_t i) {
            prog.blocks.at(i).chord = withSeventhToggled(prog.key.mode, prog.blocks.at(i).chord);
        });
    };
    sus2Button.onClick = [this] {
        applyToSelected("Toggle sus2", [](Progression& prog, std::size_t i) {
            prog.blocks.at(i).chord = withSuspensionToggled(prog.blocks.at(i).chord, Suspension::Sus2);
        });
    };
    sus4Button.onClick = [this] {
        applyToSelected("Toggle sus4", [](Progression& prog, std::size_t i) {
            prog.blocks.at(i).chord = withSuspensionToggled(prog.blocks.at(i).chord, Suspension::Sus4);
        });
    };
    ninthButton.setEnabled(false);  // 9th chords arrive with milestone 5 (extended harmony)
    splitButton.onClick = [this] { splitClicked(); };
    duplicateButton.onClick = [this] { duplicateClicked(); };
    deleteButton.onClick = [this] { deleteClicked(); };
    for (auto* b : {&triadButton, &seventhButton, &ninthButton, &sus2Button, &sus4Button, &splitButton, &duplicateButton, &deleteButton}) {
        addAndMakeVisible(*b);
    }

    // tools
    undoButton.onClick = [this] {
        const auto label = chordProcessor.getHistoryInfo().undoLabel;
        if (chordProcessor.undo()) setStatus("Undid: " + juce::String(label));
    };
    redoButton.onClick = [this] {
        const auto label = chordProcessor.getHistoryInfo().redoLabel;
        if (chordProcessor.redo()) setStatus("Redid: " + juce::String(label));
    };
    snapBox.addItem("Snap: Bar", 1);
    snapBox.addItem("Snap: Beat", 2);
    snapBox.addItem("Snap: 1/2 beat", 3);
    snapBox.addItem("Snap: Off", 4);
    snapBox.setSelectedId(1, juce::dontSendNotification);
    styleCombo(snapBox);
    snapBox.onChange = [this] {
        const SnapMode modes[4] = {SnapMode::Bar, SnapMode::Beat, SnapMode::HalfBeat, SnapMode::Off};
        timeline.setSnapMode(modes[std::clamp(snapBox.getSelectedId(), 1, 4) - 1]);
    };
    tempoSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 84, 24);
    tempoSlider.setRange(20.0, 400.0, 0.1);
    tempoSlider.setTextValueSuffix(" BPM");
    tempoSlider.setColour(juce::Slider::textBoxTextColourId, ui::textColour());
    tempoSlider.setColour(juce::Slider::textBoxOutlineColourId, ui::controlOutlineColour());
    tempoSlider.setColour(juce::Slider::trackColourId, ui::mutedTextColour());
    tempoSlider.setColour(juce::Slider::thumbColourId, ui::textColour());
    tempoSlider.onValueChange = [this] { tempoChanged(); };
    exportButton.onClick = [this] { exportMidiClicked(); };
    for (auto* c : std::initializer_list<juce::Component*>{&undoButton, &redoButton, &snapBox, &tempoSlider, &exportButton}) addAndMakeVisible(*c);

    // footer
    footerNote.setText("Syncs to the DAW when it plays", juce::dontSendNotification);
    previewButton.onClick = [this] { previewClicked(); };
    styleLabel(footerNote, 12.0f, false, ui::mutedTextColour());
    copyButton.onClick = [this] { copyClicked(); };
    pasteButton.onClick = [this] { pasteClicked(); };
    styleLabel(statusLabel, 12.0f, false, ui::mutedTextColour());
    for (auto* c : std::initializer_list<juce::Component*>{&previewButton, &footerNote, &copyButton, &pasteButton, &statusLabel}) addAndMakeVisible(*c);

    setSize(640, 620);
    refreshFromModel();
    chordProcessor.changes().addChangeListener(this);
    startTimerHz(15);
}

ChordSketchEditor::~ChordSketchEditor() { chordProcessor.changes().removeChangeListener(this); }

void ChordSketchEditor::paint(juce::Graphics& g) {
    g.fillAll(ui::backgroundColour());
    g.setColour(ui::panelBorderColour());
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(6.0f), 16.0f, 1.5f);

    g.setColour(juce::Colour(0xff1c1c20));
    g.fillRoundedRectangle(iconBounds.toFloat(), 10.0f);
    g.setColour(ui::textColour());
    g.setFont(ui::font(22.0f));
    g.drawText(juce::String::fromUTF8("\xE2\x99\xAA"), iconBounds, juce::Justification::centred);

    g.setColour(juce::Colour(0xff1c1c20));
    g.fillRoundedRectangle(badgeBounds.toFloat(), static_cast<float>(badgeBounds.getHeight()) * 0.5f);
    g.setColour(ui::textColour());
    g.setFont(ui::font(12.0f, true));
    g.drawText("VST3", badgeBounds, juce::Justification::centred);

    // divider above the tools row
    g.setColour(ui::panelBorderColour());
    g.fillRect(24, undoButton.getY() - 12, getWidth() - 48, 1);
}

void ChordSketchEditor::resized() {
    auto area = getLocalBounds().reduced(24, 22);

    auto header = area.removeFromTop(44);
    iconBounds = header.removeFromLeft(44);
    header.removeFromLeft(12);
    badgeBounds = header.removeFromRight(56).withSizeKeepingCentre(56, 24);
    titleLabel.setBounds(header.removeFromTop(26));
    subtitleLabel.setBounds(header);
    area.removeFromTop(14);

    // key / scale / meter
    const int columnWidth = (area.getWidth() - 2 * 14) / 3;
    auto captions = area.removeFromTop(22);
    auto controls = area.removeFromTop(36);
    keyCaption.setBounds(captions.removeFromLeft(columnWidth));
    captions.removeFromLeft(14);
    scaleCaption.setBounds(captions.removeFromLeft(columnWidth));
    captions.removeFromLeft(14);
    meterCaption.setBounds(captions);
    tonicBox.setBounds(controls.removeFromLeft(columnWidth));
    controls.removeFromLeft(14);
    scaleBox.setBounds(controls.removeFromLeft(columnWidth));
    controls.removeFromLeft(14);
    meterValue.setBounds(controls.withTrimmedLeft(8));
    area.removeFromTop(16);

    // timeline
    auto timelineHeader = area.removeFromTop(22);
    timelineCaption.setBounds(timelineHeader.removeFromLeft(140));
    timelineHint.setBounds(timelineHeader);
    area.removeFromTop(6);
    timeline.setBounds(area.removeFromTop(112));
    area.removeFromTop(16);

    // palette
    paletteCaption.setBounds(area.removeFromTop(22));
    area.removeFromTop(6);
    auto palette = area.removeFromTop(44);
    const int gap = 8;
    const int buttonWidth = (palette.getWidth() - gap * (kPaletteDegrees - 1)) / kPaletteDegrees;
    for (auto& button : paletteButtons) {
        button->setBounds(palette.removeFromLeft(buttonWidth));
        palette.removeFromLeft(gap);
    }
    area.removeFromTop(16);

    // selected chord
    auto selectedHeader = area.removeFromTop(22);
    selectedCaption.setBounds(selectedHeader.removeFromLeft(160));
    selectedName.setBounds(selectedHeader);
    area.removeFromTop(6);
    auto pills = area.removeFromTop(34);
    const std::pair<juce::Component*, int> left[] = {{&triadButton, 64}, {&seventhButton, 56}, {&ninthButton, 56}, {&sus2Button, 64}, {&sus4Button, 64}};
    for (const auto& item : left) {
        item.first->setBounds(pills.removeFromLeft(item.second));
        pills.removeFromLeft(8);
    }
    deleteButton.setBounds(pills.removeFromRight(64));
    pills.removeFromRight(8);
    duplicateButton.setBounds(pills.removeFromRight(84));
    pills.removeFromRight(8);
    splitButton.setBounds(pills.removeFromRight(64));
    area.removeFromTop(26);

    // tools
    auto tools = area.removeFromTop(36);
    undoButton.setBounds(tools.removeFromLeft(64));
    tools.removeFromLeft(8);
    redoButton.setBounds(tools.removeFromLeft(64));
    tools.removeFromLeft(8);
    snapBox.setBounds(tools.removeFromLeft(124));
    tools.removeFromLeft(8);
    exportButton.setBounds(tools.removeFromRight(132));
    tools.removeFromRight(8);
    tempoSlider.setBounds(tools);
    area.removeFromTop(10);

    // footer
    auto footer = area.removeFromTop(38);
    copyButton.setBounds(footer.removeFromRight(150));
    footer.removeFromRight(8);
    pasteButton.setBounds(footer.removeFromRight(64));
    previewButton.setBounds(footer.removeFromLeft(112));
    footer.removeFromLeft(10);
    footerNote.setBounds(footer);
    area.removeFromTop(4);
    statusLabel.setBounds(area.removeFromTop(22));
}

void ChordSketchEditor::setStatus(const juce::String& text) { statusLabel.setText(text, juce::dontSendNotification); }

void ChordSketchEditor::changeListenerCallback(juce::ChangeBroadcaster*) { refreshFromModel(); }

void ChordSketchEditor::timerCallback() {
    const bool running = chordProcessor.isPreviewRunning() || chordProcessor.isPreviewRequested();
    const auto text = running ? juce::String(juce::CharPointer_UTF8("\xE2\x96\xA0  Stop")) : juce::String(juce::CharPointer_UTF8("\xE2\x96\xB6  Preview"));
    if (previewButton.getButtonText() != text) previewButton.setButtonText(text);
    previewButton.setEnabled(running || !model.blocks.empty());
}

void ChordSketchEditor::previewClicked() {
    if (chordProcessor.isPreviewRunning() || chordProcessor.isPreviewRequested()) {
        chordProcessor.setPreviewRequested(false);
        return;
    }
    if (chordProcessor.getPlayhead().playing) {
        setStatus("The DAW is playing: Preview is off while it plays (the plugin already follows the transport).");
        return;
    }
    chordProcessor.setPreviewRequested(true);
    setStatus("Previewing from bar 1 at " + juce::String(model.tempoBpm, 1) + " BPM. Needs the track's MIDI routed as before.");
}

void ChordSketchEditor::refreshFromModel() {
    model = chordProcessor.getProgression();
    const auto history = chordProcessor.getHistoryInfo();
    const int count = static_cast<int>(model.blocks.size());
    if (selected >= count) selected = count - 1;

    // key / scale / meter
    for (int pc = 0; pc < 12; ++pc) tonicBox.changeItemText(pc + 1, juce::String(noteName(model.key, pc)));
    tonicBox.setSelectedId(model.key.tonic + 1, juce::dontSendNotification);
    scaleBox.setSelectedId(model.key.mode == Mode::Major ? 1 : 2, juce::dontSendNotification);
    meterValue.setText(juce::String(model.timeSignature.numerator) + "/" + juce::String(model.timeSignature.denominator), juce::dontSendNotification);

    // timeline
    const Tick bar = model.timeSignature.ticksPerBar();
    const auto bars = static_cast<int>(std::max<Tick>(model.lengthTicks() / bar, 4));
    timelineHint.setText(juce::String(bars) + " bars \xC2\xB7 click to select, drag to move, drag edges to resize", juce::dontSendNotification);
    timeline.setProgression(model);
    timeline.setSelectedIndex(selected);

    // palette: the seven diatonic chords of the current scale
    const bool hasSelection = selected >= 0;
    const ChordSpec* current = hasSelection ? &model.blocks[static_cast<std::size_t>(selected)].chord : nullptr;
    for (int degree = 1; degree <= kPaletteDegrees; ++degree) {
        auto& button = *paletteButtons[static_cast<std::size_t>(degree - 1)];
        const auto roman = formatRomanNumeral(diatonicChord(model.key.mode, degree));
        button.setButtonText(ui::displayNumeral(roman ? juce::String(*roman) : juce::String("?")));
        button.setEnabled(hasSelection);
        button.setToggleState(current != nullptr && current->degree == degree && current->accidental == 0, juce::dontSendNotification);
    }

    // selected chord
    selectedName.setText(current != nullptr ? juce::String(chordName(model.key, *current)) : juce::String(juce::CharPointer_UTF8("\xE2\x80\x93")),
                         juce::dontSendNotification);
    for (auto* b : {&triadButton, &seventhButton, &splitButton, &duplicateButton, &deleteButton}) b->setEnabled(hasSelection);
    sus2Button.setEnabled(current != nullptr && canSuspend(*current));
    sus4Button.setEnabled(current != nullptr && canSuspend(*current));
    triadButton.setToggleState(current != nullptr && current->seventh == Seventh::None && current->suspension == Suspension::None, juce::dontSendNotification);
    seventhButton.setToggleState(current != nullptr && current->seventh != Seventh::None, juce::dontSendNotification);
    sus2Button.setToggleState(current != nullptr && current->suspension == Suspension::Sus2, juce::dontSendNotification);
    sus4Button.setToggleState(current != nullptr && current->suspension == Suspension::Sus4, juce::dontSendNotification);

    // tools
    undoButton.setEnabled(history.canUndo);
    redoButton.setEnabled(history.canRedo);
    tempoSlider.setValue(model.tempoBpm, juce::dontSendNotification);

    const auto loadError = chordProcessor.getLastLoadError();
    if (!loadError.empty()) setStatus("Could not restore the saved project (kept the current one): " + juce::String(loadError));
}

void ChordSketchEditor::selectionChanged(int index) {
    selected = index;
    refreshFromModel();
}

void ChordSketchEditor::applyToSelected(const std::string& label,
                                        const std::function<void(Progression&, std::size_t)>& change) {
    if (selected < 0) return;
    const auto index = static_cast<std::size_t>(selected);
    const auto result = chordProcessor.editProgression(label, [&change, index](Progression& prog) { change(prog, index); });
    if (!result.ok) setStatus("Cannot do that: " + juce::String(result.error));
}

void ChordSketchEditor::paletteClicked(int degree) {
    applyToSelected("Change chord", [degree](Progression& prog, std::size_t i) {
        prog.blocks.at(i).chord = withDegree(prog.key.mode, prog.blocks.at(i).chord, degree);
    });
}

void ChordSketchEditor::keyChanged() {
    const int tonic = tonicBox.getSelectedId() - 1;
    if (tonic < 0 || tonic == model.key.tonic) return;
    const auto result = chordProcessor.editProgression("Change key", [tonic](Progression& prog) { prog.key.tonic = tonic; });
    if (!result.ok) setStatus("Cannot change key: " + juce::String(result.error));
}

void ChordSketchEditor::scaleChanged() {
    const auto mode = scaleBox.getSelectedId() == 2 ? Mode::Minor : Mode::Major;
    if (mode == model.key.mode) return;
    const auto result = chordProcessor.editProgression("Change scale", [mode](Progression& prog) { changeMode(prog, mode); });
    if (!result.ok) setStatus("Cannot change scale: " + juce::String(result.error));
}

void ChordSketchEditor::tempoChanged() {
    const double bpm = tempoSlider.getValue();
    const auto result = chordProcessor.editProgression("Change tempo", [bpm](Progression& prog) { prog.tempoBpm = bpm; }, "tempo");
    if (!result.ok) setStatus("Cannot change tempo: " + juce::String(result.error));
}

void ChordSketchEditor::splitClicked() {
    if (selected < 0) return;
    const auto index = static_cast<std::size_t>(selected);
    const auto unit = snapUnitTicks(SnapMode::Beat, model.timeSignature);
    const auto result = chordProcessor.editProgression("Split chord", [index, unit](Progression& prog) {
        const auto& block = prog.blocks.at(index);
        const Tick middle = block.startTick + block.durationTicks / 2;
        auto r = splitBlock(prog, index, middle, unit);
        if (!r.ok) r = splitBlock(prog, index, middle, 1);  // too short for the beat grid: cut exactly in half
        if (!r.ok) throw std::runtime_error(r.error);
    });
    if (result.ok) selected += 1;  // the second half
    else setStatus("Cannot split: " + juce::String(result.error));
}

void ChordSketchEditor::duplicateClicked() {
    if (selected < 0) return;
    const auto index = static_cast<std::size_t>(selected);
    const auto result = chordProcessor.editProgression("Duplicate chord", [index](Progression& prog) {
        const auto r = duplicateBlock(prog, index);
        if (!r.ok) throw std::runtime_error(r.error);
    });
    if (result.ok) selected += 1;  // the copy
    else setStatus("Cannot duplicate: " + juce::String(result.error));
}

void ChordSketchEditor::deleteClicked() {
    if (selected < 0) return;
    const auto index = static_cast<std::size_t>(selected);
    const auto result = chordProcessor.editProgression("Delete chord", [index](Progression& prog) {
        const auto r = deleteBlock(prog, index);
        if (!r.ok) throw std::runtime_error(r.error);
    });
    if (!result.ok) setStatus("Cannot delete: " + juce::String(result.error));
    // the selection is clamped to the remaining blocks when the model refreshes
}

void ChordSketchEditor::copyClicked() {
    const auto text = formatProgressionText(model);
    juce::SystemClipboard::copyTextToClipboard(juce::String(text));
    setStatus("Copied: " + juce::String(text));
}

void ChordSketchEditor::pasteClicked() {
    const auto text = juce::SystemClipboard::getTextFromClipboard().toStdString();
    const auto parsed = parseProgressionText(text, model.timeSignature);
    if (!parsed.ok) {
        setStatus("Cannot paste: " + juce::String(parsed.error));
        return;
    }
    const auto result = chordProcessor.editProgression("Paste progression", [&parsed](Progression& prog) { prog.blocks = parsed.blocks; });
    if (result.ok) {
        selected = parsed.blocks.empty() ? -1 : 0;
        setStatus("Pasted " + juce::String(static_cast<int>(parsed.blocks.size())) + " chords");
    } else {
        setStatus("Cannot paste: " + juce::String(result.error));
    }
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
        const auto bytes = chordProcessor.exportMidiBytes();
        if (file.replaceWithData(bytes.data(), bytes.size())) {
            setStatus("Wrote " + juce::String((int) bytes.size()) + " bytes to " + file.getFullPathName());
        } else {
            setStatus("Could not write " + file.getFullPathName());
        }
    } catch (const std::exception& e) {
        setStatus(juce::String("Export failed: ") + e.what());
    }
}
