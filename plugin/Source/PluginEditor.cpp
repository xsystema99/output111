#include "PluginEditor.h"

#include <chordcore/ChordEdit.h>
#include <chordcore/MelodyGenerator.h>
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
constexpr int kBaseWidth = 640, kBaseHeight = 968;  // the smallest size: the layout below is tuned for it

}  // namespace

ChordSketchEditor::ChordSketchEditor(ChordSketchProcessor& p)
    : juce::AudioProcessorEditor(&p), chordProcessor(p), model(p.getProgression()), timeline(p), roll(p, timeline) {

    // header
    titleLabel.setText("ChordSketch", juce::dontSendNotification);
    styleLabel(titleLabel, 20.0f, true, ui::textColour());
    subtitleLabel.setText("Chord sequencer", juce::dontSendNotification);
    styleLabel(subtitleLabel, 12.0f, false, ui::mutedTextColour());
    addAndMakeVisible(titleLabel);
    addAndMakeVisible(subtitleLabel);

    // key / scale / meter
    for (auto* caption : {&keyCaption, &scaleCaption, &octaveCaption, &lengthCaption, &meterCaption}) {
        styleLabel(*caption, 14.0f, true, ui::textColour());
        addAndMakeVisible(*caption);
    }
    for (int i = 0; i < 12; ++i) tonicBox.addItem("C", i + 1);  // names are set in refreshFromModel (they depend on the scale)
    scaleBox.addItem("Major", 1);
    scaleBox.addItem("Minor", 2);
    for (int o = -2; o <= 2; ++o) octaveBox.addItem(o > 0 ? "+" + juce::String(o) : juce::String(o), o + 3);
    lengthBox.addItem("Auto", 1);
    lengthBox.addItem("4 bars", 2);
    lengthBox.addItem("8 bars", 3);
    lengthBox.addItem("16 bars", 4);
    lengthBox.addItem("32 bars", 5);
    styleCombo(tonicBox);
    styleCombo(scaleBox);
    styleCombo(octaveBox);
    styleCombo(lengthBox);
    tonicBox.onChange = [this] { keyChanged(); };
    scaleBox.onChange = [this] { scaleChanged(); };
    octaveBox.onChange = [this] { octaveChanged(); };
    lengthBox.onChange = [this] { lengthChanged(); };
    addAndMakeVisible(tonicBox);
    addAndMakeVisible(scaleBox);
    addAndMakeVisible(octaveBox);
    addAndMakeVisible(lengthBox);
    styleLabel(meterValue, 17.0f, true, ui::textColour(), juce::Justification::centredLeft);
    addAndMakeVisible(meterValue);

    // timeline
    styleLabel(timelineCaption, 14.0f, true, ui::textColour());
    styleLabel(timelineHint, 12.0f, false, ui::mutedTextColour(), juce::Justification::centredRight);
    addAndMakeVisible(timelineCaption);
    addAndMakeVisible(timelineHint);
    snapBox.addItem("Snap: 1 bar", 1);
    snapBox.addItem("Snap: 1/2", 2);
    snapBox.addItem("Snap: 1/4", 3);
    snapBox.addItem("Snap: 1/8", 4);
    snapBox.addItem("Snap: 1/16", 5);
    snapBox.addItem("Snap: 1/32", 6);
    snapBox.addItem("Snap: 1/8 triplet", 7);
    snapBox.addItem("Snap: 1/16 triplet", 8);
    snapBox.addItem("Snap: Off", 9);
    snapBox.setSelectedId(4, juce::dontSendNotification);
    styleCombo(snapBox);
    snapBox.onChange = [this] {
        static const SnapMode modes[9] = {SnapMode::Bar, SnapMode::HalfNote, SnapMode::QuarterNote, SnapMode::EighthNote, SnapMode::SixteenthNote,
                                          SnapMode::ThirtySecondNote, SnapMode::EighthTriplet, SnapMode::SixteenthTriplet, SnapMode::Off};
        timeline.setSnapMode(modes[std::clamp(snapBox.getSelectedId(), 1, 9) - 1]);
    };
    insertLengthBox.addItem("Insert: 1/16", 1);
    insertLengthBox.addItem("Insert: 1/8", 2);
    insertLengthBox.addItem("Insert: 1/4", 3);
    insertLengthBox.addItem("Insert: 1/2", 4);
    insertLengthBox.addItem("Insert: 1 bar", 5);
    insertLengthBox.setSelectedId(3, juce::dontSendNotification);
    styleCombo(insertLengthBox);
    insertLengthBox.onChange = [this] {
        static const Tick lengths[4] = {240, 480, 960, 1920};
        const int id = std::clamp(insertLengthBox.getSelectedId(), 1, 5);
        timeline.setInsertDuration(id == 5 ? model.timeSignature.ticksPerBar() : lengths[id - 1]);
    };
    fitButton.onClick = [this] { timeline.fitToWindow(); };
    for (auto* c : std::initializer_list<juce::Component*>{&snapBox, &insertLengthBox, &fitButton}) addAndMakeVisible(*c);
    timeline.setSnapMode(SnapMode::EighthNote);
    timeline.setInsertDuration(kTicksPerQuarter);
    timeline.onSelectionChanged = [this](const std::vector<std::size_t>&, int) { selectionChanged(); };
    timeline.onMessage = [this](const juce::String& text) { setStatus(text); };
    addAndMakeVisible(timeline);

    // melody piano roll (shares the grid's zoom, scroll and snap)
    styleLabel(melodyCaption, 14.0f, true, ui::textColour());
    styleLabel(velocityCaption, 12.0f, false, ui::mutedTextColour());
    styleLabel(melodyHint, 11.0f, false, ui::mutedTextColour(), juce::Justification::centredRight);
    melodyHint.setText("bright = chord, faint = scale, hatched = outside", juce::dontSendNotification);
    mappingBox.addItem("Diatonic colours", 1);
    mappingBox.addItem("Major-centric", 2);
    paletteBox.addItem("Classic", 1);
    paletteBox.addItem("Soft", 2);
    paletteBox.addItem("Colour-blind", 3);
    mappingBox.setTooltip("Diatonic: colours follow the key's own scale. Major-centric: colours follow the interval above the tonic.");
    paletteBox.setTooltip("Note colour palette");
    labelsBox.addItem("Labels: None", 1);
    labelsBox.addItem("Labels: Pitch", 2);
    labelsBox.addItem("Labels: Degree", 3);
    labelsBox.addItem("Labels: Both", 4);
    for (auto* b : {&mappingBox, &paletteBox, &labelsBox}) {
        styleCombo(*b);
        b->onChange = [this] { melodySettingsChanged(); };
        addAndMakeVisible(*b);
    }
    mappingBox.setSelectedId(1, juce::dontSendNotification);
    paletteBox.setSelectedId(1, juce::dontSendNotification);
    labelsBox.setSelectedId(3, juce::dontSendNotification);
    velocitySlider.setSliderStyle(juce::Slider::LinearHorizontal);
    velocitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 20);
    velocitySlider.setRange(1.0, 127.0, 1.0);
    velocitySlider.setValue(90.0, juce::dontSendNotification);
    velocitySlider.setEnabled(false);
    velocitySlider.onValueChange = [this] { roll.setSelectedVelocity(static_cast<int>(velocitySlider.getValue())); };
    muteChordsButton.setClickingTogglesState(true);
    muteMelodyButton.setClickingTogglesState(true);
    muteChordsButton.onClick = [this] { muteChanged(); };
    muteMelodyButton.onClick = [this] { muteChanged(); };
    muteChordsButton.setTooltip("Silence the chords in playback (channel 1). Exports are not affected.");
    muteMelodyButton.setTooltip("Silence the melody in playback (channel 2). Exports are not affected.");
    addAndMakeVisible(muteChordsButton);
    addAndMakeVisible(muteMelodyButton);
    generateButton.onClick = [this] { openGenerator(); };
    addAndMakeVisible(generateButton);
    lockButton.setEnabled(false);
    lockButton.onClick = [this] { roll.toggleLockSelected(); };
    roll.onMessage = [this](const juce::String& text) { setStatus(text); };
    roll.onSelectionChanged = [this] { melodySelectionChanged(); };
    timeline.onViewChanged = [this] { roll.viewChanged(); };
    for (auto* c : std::initializer_list<juce::Component*>{&melodyCaption, &velocityCaption, &melodyHint, &velocitySlider, &lockButton, &roll}) addAndMakeVisible(*c);

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
    styleLabel(chordOctaveLabel, 12.0f, false, ui::mutedTextColour(), juce::Justification::centred);
    chordOctaveDown.onClick = [this] {
        applyToSelected("Chord octave down", [](Progression& prog, std::size_t i) {
            const auto r = shiftBlockOctave(prog, i, -1);
            if (!r.ok) throw std::runtime_error(r.error);
        });
    };
    chordOctaveUp.onClick = [this] {
        applyToSelected("Chord octave up", [](Progression& prog, std::size_t i) {
            const auto r = shiftBlockOctave(prog, i, +1);
            if (!r.ok) throw std::runtime_error(r.error);
        });
    };
    styleLabel(inversionLabel, 12.0f, false, ui::mutedTextColour(), juce::Justification::centred);
    inversionDown.onClick = [this] {
        applyToSelected("Inversion down", [](Progression& prog, std::size_t i) {
            auto& chord = prog.blocks.at(i).chord;
            chord = withInversion(chord, chord.inversion - 1);
        });
    };
    inversionUp.onClick = [this] {
        applyToSelected("Inversion up", [](Progression& prog, std::size_t i) {
            auto& chord = prog.blocks.at(i).chord;
            chord = withInversion(chord, chord.inversion + 1);
        });
    };
    addAndMakeVisible(inversionLabel);
    addAndMakeVisible(inversionDown);
    addAndMakeVisible(inversionUp);
    addAndMakeVisible(selectedCaption);
    addAndMakeVisible(selectedName);
    addAndMakeVisible(chordOctaveLabel);
    addAndMakeVisible(chordOctaveDown);
    addAndMakeVisible(chordOctaveUp);

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
    ninthButton.onClick = [this] {
        applyToSelected("Toggle 9th", [](Progression& prog, std::size_t i) {
            prog.blocks.at(i).chord = withNinthToggled(prog.key.mode, prog.blocks.at(i).chord);
        });
    };
    eleventhButton.onClick = [this] {
        applyToSelected("Toggle 11th", [](Progression& prog, std::size_t i) {
            prog.blocks.at(i).chord = withEleventhToggled(prog.key.mode, prog.blocks.at(i).chord);
        });
    };
    thirteenthButton.onClick = [this] {
        applyToSelected("Toggle 13th", [](Progression& prog, std::size_t i) {
            prog.blocks.at(i).chord = withThirteenthToggled(prog.key.mode, prog.blocks.at(i).chord);
        });
    };
    const std::pair<ui::StyledButton*, Alteration> alterations[] = {{&flat9Button, Alteration::FlatNine}, {&sharp9Button, Alteration::SharpNine},
                                                                    {&sharp11Button, Alteration::SharpEleven}, {&flat13Button, Alteration::FlatThirteen}};
    for (const auto& entry : alterations) {
        const Alteration alteration = entry.second;
        entry.first->onClick = [this, alteration] {
            applyToSelected("Alter chord", [alteration](Progression& prog, std::size_t i) {
                prog.blocks.at(i).chord = withAlterationToggled(prog.key.mode, prog.blocks.at(i).chord, alteration);
            });
        };
    }
    splitButton.onClick = [this] { splitClicked(); };
    duplicateButton.onClick = [this] { duplicateClicked(); };
    deleteButton.onClick = [this] { deleteClicked(); };
    for (auto* b : {&triadButton, &seventhButton, &ninthButton, &sus2Button, &sus4Button, &eleventhButton, &thirteenthButton, &flat9Button,
                    &sharp9Button, &sharp11Button, &flat13Button, &splitButton, &duplicateButton, &deleteButton}) {
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
    for (auto* c : std::initializer_list<juce::Component*>{&undoButton, &redoButton, &tempoSlider, &exportButton}) addAndMakeVisible(*c);

    // footer
    dragHandle.makeFile = [this](juce::String& error) { return writeDragFile(error); };
    dragHandle.onMessage = [this](const juce::String& text) { setStatus(text); };
    dragPartBox.addItem("Drag: both", 1);
    dragPartBox.addItem("Drag: chords", 2);
    dragPartBox.addItem("Drag: melody", 3);
    dragPartBox.setSelectedId(1, juce::dontSendNotification);
    dragPartBox.setTooltip("Which MIDI file the drag handle hands to Live (both = Format 1 file with a Chords and a Melody track)");
    styleCombo(dragPartBox);
    previewButton.onClick = [this] { previewClicked(); };
    copyButton.onClick = [this] { copyClicked(); };
    pasteButton.onClick = [this] { pasteClicked(); };
    styleLabel(statusLabel, 12.0f, false, ui::mutedTextColour());
    for (auto* c : std::initializer_list<juce::Component*>{&previewButton, &dragHandle, &dragPartBox, &copyButton, &pasteButton, &statusLabel}) addAndMakeVisible(*c);

    setResizeLimits(kBaseWidth, kBaseHeight, 1600, 1200);
    setResizable(true, true);  // the corner handle; extra room goes to the grid
    setSize(kBaseWidth, kBaseHeight);
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

    // key / scale / octave / length / meter
    const int columnGap = 12;
    const int columnWidths[4] = {128, 128, 96, 116};
    auto captions = area.removeFromTop(22);
    auto controls = area.removeFromTop(36);
    juce::Label* captionLabels[4] = {&keyCaption, &scaleCaption, &octaveCaption, &lengthCaption};
    juce::ComboBox* boxes[4] = {&tonicBox, &scaleBox, &octaveBox, &lengthBox};
    for (int i = 0; i < 4; ++i) {
        captionLabels[i]->setBounds(captions.removeFromLeft(columnWidths[i]));
        captions.removeFromLeft(columnGap);
        boxes[i]->setBounds(controls.removeFromLeft(columnWidths[i]));
        controls.removeFromLeft(columnGap);
    }
    meterCaption.setBounds(captions);
    meterValue.setBounds(controls.withTrimmedLeft(4));
    area.removeFromTop(16);

    // timeline
    auto timelineHeader = area.removeFromTop(28);
    timelineCaption.setBounds(timelineHeader.removeFromLeft(86));
    snapBox.setBounds(timelineHeader.removeFromLeft(136));
    timelineHeader.removeFromLeft(8);
    insertLengthBox.setBounds(timelineHeader.removeFromLeft(122));
    timelineHeader.removeFromLeft(8);
    fitButton.setBounds(timelineHeader.removeFromLeft(48));
    timelineHint.setBounds(timelineHeader.withTrimmedLeft(8));
    area.removeFromTop(6);
    // a taller window gives its extra height to the grid
    timeline.setBounds(area.removeFromTop(150 + std::max(0, getHeight() - kBaseHeight)));
    area.removeFromTop(8);

    // melody piano roll
    auto melodyHeader = area.removeFromTop(26);
    melodyCaption.setBounds(melodyHeader.removeFromLeft(56));
    mappingBox.setBounds(melodyHeader.removeFromLeft(128));
    melodyHeader.removeFromLeft(6);
    paletteBox.setBounds(melodyHeader.removeFromLeft(108));
    melodyHeader.removeFromLeft(6);
    labelsBox.setBounds(melodyHeader.removeFromLeft(112));
    melodyHeader.removeFromLeft(6);
    muteMelodyButton.setBounds(melodyHeader.removeFromRight(88));
    melodyHeader.removeFromRight(6);
    muteChordsButton.setBounds(melodyHeader.removeFromRight(88));
    area.removeFromTop(4);
    roll.setBounds(area.removeFromTop(172));
    area.removeFromTop(4);
    auto melodyFooter = area.removeFromTop(26);
    velocityCaption.setBounds(melodyFooter.removeFromLeft(58));
    velocitySlider.setBounds(melodyFooter.removeFromLeft(168));
    melodyFooter.removeFromLeft(8);
    lockButton.setBounds(melodyFooter.removeFromLeft(56));
    melodyFooter.removeFromLeft(8);
    generateButton.setBounds(melodyFooter.removeFromLeft(100));
    melodyHint.setBounds(melodyFooter.withTrimmedLeft(6));
    area.removeFromTop(12);

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
    selectedCaption.setBounds(selectedHeader.removeFromLeft(112));
    selectedName.setBounds(selectedHeader.removeFromRight(62));
    selectedHeader.removeFromRight(6);
    chordOctaveUp.setBounds(selectedHeader.removeFromRight(28));
    chordOctaveLabel.setBounds(selectedHeader.removeFromRight(76));
    chordOctaveDown.setBounds(selectedHeader.removeFromRight(28));
    selectedHeader.removeFromRight(10);
    inversionUp.setBounds(selectedHeader.removeFromRight(28));
    inversionLabel.setBounds(selectedHeader.removeFromRight(70));
    inversionDown.setBounds(selectedHeader.removeFromRight(28));
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
    area.removeFromTop(6);
    auto extensions = area.removeFromTop(34);
    for (auto* b : {&eleventhButton, &thirteenthButton, &flat9Button, &sharp9Button, &sharp11Button, &flat13Button}) {
        b->setBounds(extensions.removeFromLeft(b == &eleventhButton || b == &thirteenthButton ? 64 : 56));
        extensions.removeFromLeft(8);
    }
    area.removeFromTop(26);

    // tools
    auto tools = area.removeFromTop(36);
    undoButton.setBounds(tools.removeFromLeft(64));
    tools.removeFromLeft(8);
    redoButton.setBounds(tools.removeFromLeft(64));
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
    dragHandle.setBounds(footer.removeFromLeft(150).withSizeKeepingCentre(150, 30));
    footer.removeFromLeft(8);
    dragPartBox.setBounds(footer.removeFromLeft(104).withSizeKeepingCentre(104, 30));
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
    timeline.setProgression(model);  // also drops selection indices that no longer exist
    roll.setProgression(model);
    if (generatorPanel != nullptr) generatorPanel->refresh();
    mappingBox.setSelectedId(model.melodySettings.mapping == ColourMapping::DiatonicCentric ? 1 : 2, juce::dontSendNotification);
    paletteBox.setSelectedId(static_cast<int>(model.melodySettings.palette) + 1, juce::dontSendNotification);
    labelsBox.setSelectedId(static_cast<int>(model.melodySettings.labels) + 1, juce::dontSendNotification);
    muteChordsButton.setToggleState(model.melodySettings.muteChords, juce::dontSendNotification);
    muteMelodyButton.setToggleState(model.melodySettings.muteMelody, juce::dontSendNotification);
    melodySelectionChanged();
    selected = timeline.getPrimary();

    // key / scale / meter
    for (int pc = 0; pc < 12; ++pc) tonicBox.changeItemText(pc + 1, juce::String(noteName(model.key, pc)));
    tonicBox.setSelectedId(model.key.tonic + 1, juce::dontSendNotification);
    scaleBox.setSelectedId(model.key.mode == Mode::Major ? 1 : 2, juce::dontSendNotification);
    meterValue.setText(juce::String(model.timeSignature.numerator) + "/" + juce::String(model.timeSignature.denominator), juce::dontSendNotification);

    octaveBox.setSelectedId(model.octave + 3, juce::dontSendNotification);
    switch (model.lengthBars) {
        case 0: lengthBox.setSelectedId(1, juce::dontSendNotification); break;
        case 4: lengthBox.setSelectedId(2, juce::dontSendNotification); break;
        case 8: lengthBox.setSelectedId(3, juce::dontSendNotification); break;
        case 16: lengthBox.setSelectedId(4, juce::dontSendNotification); break;
        case 32: lengthBox.setSelectedId(5, juce::dontSendNotification); break;
        default: lengthBox.setText(juce::String(model.lengthBars) + " bars", juce::dontSendNotification); break;  // from a loaded project
    }

    // timeline
    const Tick bar = model.timeSignature.ticksPerBar();
    const auto bars = static_cast<int>(std::max<Tick>(model.lengthTicks() / bar, 4));
    timelineHint.setText(juce::String(bars) + " bars \xC2\xB7 Ctrl+wheel zooms", juce::dontSendNotification);

    timeline.setInsertChord(diatonicChord(model.key.mode, insertDegree));

    // palette: the seven diatonic chords of the current scale
    const bool hasSelection = selected >= 0;
    const ChordSpec* current = hasSelection ? &model.blocks[static_cast<std::size_t>(selected)].chord : nullptr;
    for (int degree = 1; degree <= kPaletteDegrees; ++degree) {
        auto& button = *paletteButtons[static_cast<std::size_t>(degree - 1)];
        const auto roman = formatRomanNumeral(diatonicChord(model.key.mode, degree));
        button.setButtonText(ui::displayNumeral(roman ? juce::String(*roman) : juce::String("?")));
        button.setEnabled(true);  // always: with nothing selected it arms the chord that a grid click inserts
        const int shownDegree = current != nullptr ? current->degree : insertDegree;
        button.setToggleState(shownDegree == degree && (current == nullptr || current->accidental == 0), juce::dontSendNotification);
    }

    // selected chord
    selectedName.setText(current != nullptr ? juce::String(chordName(model.key, *current)) : juce::String(juce::CharPointer_UTF8("\xE2\x80\x93")),
                         juce::dontSendNotification);
    for (auto* b : {&triadButton, &seventhButton, &ninthButton, &eleventhButton, &thirteenthButton, &flat9Button, &sharp9Button, &sharp11Button,
                    &flat13Button, &splitButton, &duplicateButton, &deleteButton}) b->setEnabled(hasSelection);
    const int chordOctave = hasSelection ? model.blocks[static_cast<std::size_t>(selected)].octave : 0;
    chordOctaveLabel.setText(hasSelection ? "Octave " + juce::String(chordOctave > 0 ? "+" : "") + juce::String(chordOctave) : juce::String("Octave"),
                             juce::dontSendNotification);
    chordOctaveDown.setEnabled(hasSelection && chordOctave > -kMaxOctaveShift);
    chordOctaveUp.setEnabled(hasSelection && chordOctave < kMaxOctaveShift);
    {
        static const char* const names[5] = {"Root pos.", "1st inv.", "2nd inv.", "3rd inv.", "4th inv."};
        const int inv = current != nullptr ? current->inversion : 0;
        inversionLabel.setText(current != nullptr ? juce::String(names[std::clamp(inv, 0, 4)]) : juce::String("Inversion"), juce::dontSendNotification);
        inversionDown.setEnabled(current != nullptr && inv > 0);
        inversionUp.setEnabled(current != nullptr && inv < maxInversion(*current));
    }
    sus2Button.setEnabled(current != nullptr && canSuspend(*current));
    sus4Button.setEnabled(current != nullptr && canSuspend(*current));
    triadButton.setToggleState(current != nullptr && current->seventh == Seventh::None && current->suspension == Suspension::None, juce::dontSendNotification);
    seventhButton.setToggleState(current != nullptr && current->seventh != Seventh::None && current->ninth == Ninth::None, juce::dontSendNotification);
    ninthButton.setToggleState(current != nullptr && current->ninth != Ninth::None, juce::dontSendNotification);
    eleventhButton.setToggleState(current != nullptr && current->eleventh != Eleventh::None, juce::dontSendNotification);
    thirteenthButton.setToggleState(current != nullptr && current->thirteenth != Thirteenth::None, juce::dontSendNotification);
    flat9Button.setToggleState(current != nullptr && current->ninth == Ninth::Minor, juce::dontSendNotification);
    sharp9Button.setToggleState(current != nullptr && current->ninth == Ninth::Sharp, juce::dontSendNotification);
    sharp11Button.setToggleState(current != nullptr && current->eleventh == Eleventh::Sharp, juce::dontSendNotification);
    flat13Button.setToggleState(current != nullptr && current->thirteenth == Thirteenth::Minor, juce::dontSendNotification);
    sus2Button.setToggleState(current != nullptr && current->suspension == Suspension::Sus2, juce::dontSendNotification);
    sus4Button.setToggleState(current != nullptr && current->suspension == Suspension::Sus4, juce::dontSendNotification);

    // tools
    undoButton.setEnabled(history.canUndo);
    redoButton.setEnabled(history.canRedo);
    tempoSlider.setValue(model.tempoBpm, juce::dontSendNotification);

    const auto loadError = chordProcessor.getLastLoadError();
    if (!loadError.empty()) setStatus("Could not restore the saved project (kept the current one): " + juce::String(loadError));
}

void ChordSketchEditor::selectionChanged() { refreshFromModel(); }

void ChordSketchEditor::melodySelectionChanged() {
    const int count = roll.selectedCount();
    velocitySlider.setEnabled(count > 0);
    lockButton.setEnabled(count > 0);
    if (count > 0) velocitySlider.setValue(roll.primaryVelocity(), juce::dontSendNotification);
    lockButton.setButtonText(count > 0 && roll.primaryLocked() ? "Unlock" : "Lock");
}

void ChordSketchEditor::openGenerator() {
    auto panel = std::make_unique<GeneratorPanel>(chordProcessor);
    panel->onGenerate = [this] { runGenerator(0); };
    panel->onReroll = [this] { runGenerator(1); };
    panel->onSelectedRegion = [this] { runGenerator(2); };
    panel->onWholeProject = [this] { runGenerator(3); };
    panel->onAffected = [this] { runGenerator(4); };
    generatorPanel = panel.get();
    juce::CallOutBox::launchAsynchronously(std::move(panel), generateButton.getScreenBounds(), nullptr);
}

// Where "Generate" starts and what "selected region" means: the selected chords' span if any chords are selected,
// otherwise the selected melody notes' span, otherwise nothing (bar 1).
void ChordSketchEditor::currentSpan(int& startBar, Tick& from, Tick& to) const {
    const Tick bar = model.timeSignature.ticksPerBar();
    startBar = 1;
    from = to = 0;
    const auto chords = timeline.getSelection();
    const auto notes = roll.getSelection();
    bool have = false;
    for (auto i : chords) {
        if (i >= model.blocks.size()) continue;
        const auto& b = model.blocks[i];
        from = have ? std::min(from, b.startTick) : b.startTick;
        to = have ? std::max(to, b.endTick()) : b.endTick();
        have = true;
    }
    if (!have) {
        for (auto i : notes) {
            if (i >= model.melody.size()) continue;
            const auto& n = model.melody[i];
            from = have ? std::min(from, n.startTick) : n.startTick;
            to = have ? std::max(to, n.endTick()) : n.endTick();
            have = true;
        }
    }
    if (have) startBar = static_cast<int>(from / bar) + 1;
}

void ChordSketchEditor::runGenerator(int action) {
    int startBar = 1;
    Tick from = 0, to = 0;
    currentSpan(startBar, from, to);
    const auto settings = chordProcessor.getProgression().melodySettings.generator;
    std::size_t count = 0;
    GenerationRange range{};
    juce::String what;

    const auto result = chordProcessor.editProgression(action == 1 ? "Reroll melody" : action == 4 ? "Regenerate affected melody" : "Generate melody", [&](Progression& p) {
        OpResult r;
        if (action == 4) {
            r = regenerateAffectedMelody(p);
        } else {
            if (action == 1) p.melodySettings.generator.seed += 1;
            const auto scope = action == 2 ? PhraseScope::SelectedRegion : action == 3 ? PhraseScope::FullProgression : settings.scope;
            range = scopeRange(p, scope, startBar, from, to);
            r = regenerateMelody(p, range);
        }
        if (!r.ok) throw std::runtime_error(r.error);
        count = r.indices.size();
    });
    if (!result.ok) {
        setStatus("Melody not generated: " + juce::String(result.error));
        return;
    }
    const Tick bar = model.timeSignature.ticksPerBar();
    if (action == 4) {
        setStatus("Regenerated the melody over the chords that changed. Undo restores the previous melody.");
    } else {
        setStatus("Generated " + juce::String(static_cast<int>(count)) + " notes over bars " + juce::String(static_cast<int>(range.start / bar) + 1) + " to " +
                  juce::String(static_cast<int>((std::max<Tick>(range.end, 1) - 1) / bar) + 1) + " (seed " +
                  juce::String(static_cast<juce::int64>(chordProcessor.getProgression().melodySettings.generator.seed)) + "). Undo restores the previous melody.");
    }
}

void ChordSketchEditor::muteChanged() {
    const bool chords = muteChordsButton.getToggleState(), melody = muteMelodyButton.getToggleState();
    const auto result = chordProcessor.editProgression("Mute", [=](Progression& p) {
        p.melodySettings.muteChords = chords;
        p.melodySettings.muteMelody = melody;
    });
    if (!result.ok) setStatus("Could not change the mute: " + juce::String(result.error));
}

void ChordSketchEditor::melodySettingsChanged() {
    const auto mapping = mappingBox.getSelectedId() == 2 ? ColourMapping::MajorCentric : ColourMapping::DiatonicCentric;
    const auto palette = static_cast<PaletteId>(std::clamp(paletteBox.getSelectedId(), 1, 3) - 1);
    const auto labels = static_cast<LabelMode>(std::clamp(labelsBox.getSelectedId(), 1, 4) - 1);
    const auto result = chordProcessor.editProgression("Melody display", [=](Progression& p) {
        p.melodySettings.mapping = mapping;
        p.melodySettings.palette = palette;
        p.melodySettings.labels = labels;
    });
    if (!result.ok) setStatus("Could not change the melody display: " + juce::String(result.error));
}

void ChordSketchEditor::applyToSelected(const std::string& label,
                                        const std::function<void(Progression&, std::size_t)>& change) {
    const auto indices = timeline.getSelection();
    if (indices.empty()) return;
    const auto result = chordProcessor.editProgression(label, [&change, &indices](Progression& prog) {
        for (auto index : indices) change(prog, index);  // every selected chord, one undo step
    });
    if (!result.ok) setStatus("Cannot do that: " + juce::String(result.error));
}

void ChordSketchEditor::paletteClicked(int degree) {
    insertDegree = degree;
    {  // audition what the click produces (the selected chord re-targeted to this degree, or the plain diatonic chord)
        const auto indices = timeline.getSelection();
        if (!indices.empty() && indices.back() < model.blocks.size()) {
            const auto& block = model.blocks[timeline.getPrimary() >= 0 ? static_cast<std::size_t>(timeline.getPrimary()) : indices.back()];
            chordProcessor.auditionChord(withDegree(model.key.mode, block.chord, degree), block.octave);
        } else {
            chordProcessor.auditionChord(diatonicChord(model.key.mode, degree), 0);
        }
    }
    timeline.setInsertChord(diatonicChord(model.key.mode, degree));
    if (timeline.getSelection().empty()) {
        setStatus("Armed " + juce::String(chordName(model.key, diatonicChord(model.key.mode, degree))) + ": click the grid to insert it.");
        refreshFromModel();
        return;
    }
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

void ChordSketchEditor::octaveChanged() {
    const int octave = octaveBox.getSelectedId() - 3;
    if (octave < -kMaxOctaveShift || octave > kMaxOctaveShift || octave == model.octave) return;
    const auto result = chordProcessor.editProgression("Change octave", [octave](Progression& prog) { prog.octave = octave; });
    if (!result.ok) setStatus("Cannot change octave: " + juce::String(result.error));
}

void ChordSketchEditor::lengthChanged() {
    static const int bars[5] = {0, 4, 8, 16, 32};
    const int id = lengthBox.getSelectedId();
    if (id < 1 || id > 5 || bars[id - 1] == model.lengthBars) return;
    const int requested = bars[id - 1];
    const auto result = chordProcessor.editProgression("Change length", [requested](Progression& prog) { prog.lengthBars = requested; });
    if (!result.ok) setStatus("Cannot change length: " + juce::String(result.error));
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
    if (result.ok) timeline.setSelection({index + 1}, static_cast<int>(index + 1));  // the second half
    else setStatus("Cannot split: " + juce::String(result.error));
}

void ChordSketchEditor::duplicateClicked() { timeline.duplicateSelection(); }

void ChordSketchEditor::deleteClicked() { timeline.deleteSelection(); }

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
        if (parsed.blocks.empty()) timeline.setSelection({}, -1);
        else timeline.setSelection({0}, 0);
        setStatus("Pasted " + juce::String(static_cast<int>(parsed.blocks.size())) + " chords");
    } else {
        setStatus("Cannot paste: " + juce::String(result.error));
    }
}

void ChordSketchEditor::exportMidiClicked() {
    // Ask for the bars first. The range starts as the selected chords' span (or the whole project).
    const Tick bar = model.timeSignature.ticksPerBar();
    const int totalBars = std::max(1, static_cast<int>((model.lengthTicks() + bar - 1) / bar));
    int firstBar = 1, lastBar = totalBars;
    const auto selection = timeline.getSelection();
    if (!selection.empty()) {
        Tick first = model.blocks[selection.front()].startTick, last = model.blocks[selection.front()].endTick();
        for (auto i : selection) {
            first = std::min(first, model.blocks[i].startTick);
            last = std::max(last, model.blocks[i].endTick());
        }
        firstBar = static_cast<int>(first / bar) + 1;
        lastBar = static_cast<int>((last - 1) / bar) + 1;
    }

    auto* window = new juce::AlertWindow("Export MIDI", "Bars to export (the whole project is bars 1 to " + juce::String(totalBars) + ").",
                                         juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("from", juce::String(firstBar), "From bar:");
    window->addTextEditor("to", juce::String(lastBar), "To bar:");
    window->addComboBox("part", juce::StringArray{"Chords + melody (Combined.mid)", "Chords only (Chords.mid)", "Melody only (Melody.mid)"}, "Export:");
    window->addComboBox("channel", juce::StringArray{"Melody on its own channel (" + juce::String(model.melodySettings.melodyChannel) + ")", "Melody on channel 1 (compatibility)"},
                        "Melody channel:");
    window->addButton("Choose file...", 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<ChordSketchEditor> safeThis(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safeThis, window, totalBars](int result) {
        if (result != 1 || safeThis == nullptr) return;
        const int from = window->getTextEditorContents("from").getIntValue();
        const int to = window->getTextEditorContents("to").getIntValue();
        if (from < 1 || to < from || to > kMaxLengthBars) {
            safeThis->setStatus("Bar range not valid: use 1 to " + juce::String(std::max(totalBars, 1)) + " (first bar not after the last).");
            return;
        }
        ProjectExportOptions options;
        const int part = window->getComboBoxComponent("part")->getSelectedItemIndex();
        options.part = part == 1 ? ExportPart::Chords : part == 2 ? ExportPart::Melody : ExportPart::Combined;
        options.melodyOnChannel1 = window->getComboBoxComponent("channel")->getSelectedItemIndex() == 1;
        options.firstBar = from;
        options.lastBar = to;
        safeThis->chooseExportFile(options);
    }), true);
}

void ChordSketchEditor::chooseExportFile(const ProjectExportOptions& options) {
    const auto defaultFile =
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(exportFileName(options.part));

    fileChooser = std::make_unique<juce::FileChooser>("Export MIDI file", defaultFile, "*.mid");
    const auto chooserFlags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                              juce::FileBrowserComponent::warnAboutOverwriting;

    // SafePointer: the host may close the editor while the dialog is open.
    juce::Component::SafePointer<ChordSketchEditor> safeThis(this);
    fileChooser->launchAsync(chooserFlags, [safeThis, options](const juce::FileChooser& chooser) {
        if (safeThis == nullptr) return;
        auto file = chooser.getResult();
        if (file == juce::File()) return;  // cancelled
        if (!file.hasFileExtension("mid")) file = file.withFileExtension("mid");
        safeThis->writeMidiTo(file, options);
    });
}

void ChordSketchEditor::writeMidiTo(const juce::File& file, const ProjectExportOptions& options) {
    try {
        const auto bytes = chordProcessor.exportMidiBytes(options);
        if (file.replaceWithData(bytes.data(), bytes.size())) {
            setStatus("Wrote " + juce::String((int) bytes.size()) + " bytes to " + file.getFullPathName());
        } else {
            setStatus("Could not write " + file.getFullPathName());
        }
    } catch (const std::exception& e) {
        setStatus(juce::String("Export failed: ") + e.what());
    }
}

juce::File ChordSketchEditor::writeDragFile(juce::String& error) {
    try {
        ProjectExportOptions options;  // the same export path as "Export MIDI...", whole project
        const int part = dragPartBox.getSelectedItemIndex();
        options.part = part == 1 ? ExportPart::Chords : part == 2 ? ExportPart::Melody : ExportPart::Combined;
        const auto bytes = chordProcessor.exportMidiBytes(options);
        const auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ChordSketch");
        if (!dir.createDirectory().wasOk()) { error = "could not create " + dir.getFullPathName(); return {}; }
        const auto file = dir.getChildFile(exportFileName(options.part));
        if (!file.replaceWithData(bytes.data(), bytes.size())) { error = "could not write " + file.getFullPathName(); return {}; }
        return file;
    } catch (const std::exception& e) {
        error = e.what();
        return {};
    }
}
