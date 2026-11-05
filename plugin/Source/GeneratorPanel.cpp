#include "GeneratorPanel.h"

#include <algorithm>

using namespace chordcore;

namespace {

void styleCaption(juce::Label& l) {
    l.setFont(ui::font(12.0f));
    l.setColour(juce::Label::textColourId, ui::mutedTextColour());
    l.setJustificationType(juce::Justification::centredLeft);
}

void styleBox(juce::ComboBox& box) {
    box.setColour(juce::ComboBox::backgroundColourId, ui::controlFillColour());
    box.setColour(juce::ComboBox::outlineColourId, ui::controlOutlineColour());
    box.setColour(juce::ComboBox::textColourId, ui::textColour());
    box.setColour(juce::ComboBox::arrowColourId, ui::mutedTextColour());
}

void styleSlider(juce::Slider& s, double lo, double hi) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setRange(lo, hi, 1.0);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 20);
    s.setColour(juce::Slider::textBoxTextColourId, ui::textColour());
    s.setColour(juce::Slider::textBoxOutlineColourId, ui::controlOutlineColour());
}

juce::String noteText(int pitch) { return juce::MidiMessage::getMidiNoteName(pitch, true, true, 4); }

}  // namespace

GeneratorPanel::GeneratorPanel(ChordSketchProcessor& p) : chordProcessor(p) {
    for (auto* l : {&contourCaption, &densityCaption, &rhythmCaption, &scopeCaption, &tensionCaption, &lowCaption, &highCaption, &seedCaption}) {
        styleCaption(*l);
        addAndMakeVisible(*l);
    }
    contourBox.addItemList({"Ascending", "Descending", "Arch", "Inverted arch", "Wave", "Free"}, 1);
    densityBox.addItemList({"Sparse", "Moderate", "Dense"}, 1);
    rhythmBox.addItemList({"Sustained", "Lyrical", "Rhythmic", "Syncopated", "Free"}, 1);
    scopeBox.addItemList({"1 bar", "2 bars", "4 bars", "8 bars", "Selected region", "Full progression"}, 1);
    for (auto* b : {&contourBox, &densityBox, &rhythmBox, &scopeBox}) {
        styleBox(*b);
        b->onChange = [this] { settingChanged(); };
        addAndMakeVisible(*b);
    }
    styleSlider(tensionSlider, 0, 100);
    styleSlider(lowSlider, 0, 116);
    styleSlider(highSlider, 11, 127);
    lowSlider.textFromValueFunction = [](double v) { return noteText(static_cast<int>(v)); };
    highSlider.textFromValueFunction = [](double v) { return noteText(static_cast<int>(v)); };
    for (auto* s : {&tensionSlider, &lowSlider, &highSlider}) {
        s->onValueChange = [this] { settingChanged(); };
        addAndMakeVisible(*s);
    }
    seedEditor.setInputRestrictions(10, "0123456789");
    seedEditor.setColour(juce::TextEditor::backgroundColourId, ui::controlFillColour());
    seedEditor.setColour(juce::TextEditor::textColourId, ui::textColour());
    seedEditor.setColour(juce::TextEditor::outlineColourId, ui::controlOutlineColour());
    seedEditor.onReturnKey = [this] { seedEntered(); };
    seedEditor.onFocusLost = [this] { seedEntered(); };
    addAndMakeVisible(seedEditor);

    generateButton.onClick = [this] { if (onGenerate) onGenerate(); };
    rerollButton.onClick = [this] { if (onReroll) onReroll(); };
    selectedButton.onClick = [this] { if (onSelectedRegion) onSelectedRegion(); };
    wholeButton.onClick = [this] { if (onWholeProject) onWholeProject(); };
    affectedButton.onClick = [this] { if (onAffected) onAffected(); };
    for (auto* b : {&generateButton, &rerollButton, &selectedButton, &wholeButton, &affectedButton}) addAndMakeVisible(*b);

    hint.setFont(ui::font(11.0f));
    hint.setColour(juce::Label::textColourId, ui::mutedTextColour());
    hint.setJustificationType(juce::Justification::topLeft);
    hint.setText("Generate replaces the unlocked notes in the chosen length, starting at the selected chord's bar (or bar 1). Locked notes stay. Same seed = same melody. Everything is one undo step.", juce::dontSendNotification);
    addAndMakeVisible(hint);

    setSize(380, 372);
    refresh();
}

void GeneratorPanel::refresh() {
    const auto& g = chordProcessor.getProgression().melodySettings.generator;
    refreshing = true;
    contourBox.setSelectedId(static_cast<int>(g.contour) + 1, juce::dontSendNotification);
    densityBox.setSelectedId(static_cast<int>(g.density) + 1, juce::dontSendNotification);
    rhythmBox.setSelectedId(static_cast<int>(g.rhythm) + 1, juce::dontSendNotification);
    scopeBox.setSelectedId(static_cast<int>(g.scope) + 1, juce::dontSendNotification);
    tensionSlider.setValue(g.tension, juce::dontSendNotification);
    lowSlider.setValue(g.registerLow, juce::dontSendNotification);
    highSlider.setValue(g.registerHigh, juce::dontSendNotification);
    if (!seedEditor.hasKeyboardFocus(true)) seedEditor.setText(juce::String(static_cast<juce::int64>(g.seed)), false);
    refreshing = false;
}

void GeneratorPanel::settingChanged() {
    if (refreshing) return;
    const auto contour = static_cast<Contour>(std::clamp(contourBox.getSelectedId(), 1, 6) - 1);
    const auto density = static_cast<Density>(std::clamp(densityBox.getSelectedId(), 1, 3) - 1);
    const auto rhythm = static_cast<RhythmStyle>(std::clamp(rhythmBox.getSelectedId(), 1, 5) - 1);
    const auto scope = static_cast<PhraseScope>(std::clamp(scopeBox.getSelectedId(), 1, 6) - 1);
    const int tension = static_cast<int>(tensionSlider.getValue());
    int low = static_cast<int>(lowSlider.getValue());
    int high = static_cast<int>(highSlider.getValue());
    if (high - low < 11) {  // the register must span at least an octave: move the other end
        if (highSlider.isMouseButtonDown()) low = high - 11;
        else high = low + 11;
        low = std::max(0, low);
        high = std::min(127, high);
    }
    (void)chordProcessor.editProgression("Generator settings", [=](Progression& p) {
        auto& g = p.melodySettings.generator;
        g.contour = contour;
        g.density = density;
        g.rhythm = rhythm;
        g.scope = scope;
        g.tension = tension;
        g.registerLow = low;
        g.registerHigh = high;
    }, "generatorSettings");
    refreshing = true;
    lowSlider.setValue(low, juce::dontSendNotification);
    highSlider.setValue(high, juce::dontSendNotification);
    refreshing = false;
}

void GeneratorPanel::seedEntered() {
    if (refreshing) return;
    const auto value = seedEditor.getText().getLargeIntValue();
    const auto seed = static_cast<std::uint32_t>(std::clamp<juce::int64>(value, 0, 4294967295LL));
    (void)chordProcessor.editProgression("Generator seed", [=](Progression& p) { p.melodySettings.generator.seed = seed; });
}

void GeneratorPanel::paint(juce::Graphics& g) { g.fillAll(ui::backgroundColour()); }

void GeneratorPanel::resized() {
    auto area = getLocalBounds().reduced(14);
    auto row = [&](juce::Label& caption, juce::Component& control) {
        auto r = area.removeFromTop(26);
        caption.setBounds(r.removeFromLeft(74));
        control.setBounds(r);
        area.removeFromTop(6);
    };
    row(contourCaption, contourBox);
    row(densityCaption, densityBox);
    row(rhythmCaption, rhythmBox);
    row(scopeCaption, scopeBox);
    row(tensionCaption, tensionSlider);
    row(lowCaption, lowSlider);
    row(highCaption, highSlider);
    row(seedCaption, seedEditor);
    area.removeFromTop(4);
    auto buttons = area.removeFromTop(34);
    rerollButton.setBounds(buttons.removeFromRight(90));
    buttons.removeFromRight(8);
    generateButton.setBounds(buttons);
    area.removeFromTop(8);
    auto second = area.removeFromTop(28);
    const int w = (second.getWidth() - 12) / 3;
    selectedButton.setBounds(second.removeFromLeft(w));
    second.removeFromLeft(6);
    wholeButton.setBounds(second.removeFromLeft(w));
    second.removeFromLeft(6);
    affectedButton.setBounds(second);
    area.removeFromTop(8);
    hint.setBounds(area);
}
