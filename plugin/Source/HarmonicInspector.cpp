#include "HarmonicInspector.h"

#include <chordcore/ChordEdit.h>
#include <chordcore/Harmony.h>
#include <chordcore/MelodyGenerator.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/RomanNumeral.h>

#include <algorithm>
#include <exception>
#include <set>

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
    box.setWantsKeyboardFocus(false);  // the grid keeps the keyboard shortcuts
}

void styleSlider(juce::Slider& s, int low, int high, int value) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setRange(low, high, 1.0);
    s.setValue(value, juce::dontSendNotification);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
    s.setColour(juce::Slider::textBoxTextColourId, ui::textColour());
    s.setColour(juce::Slider::textBoxOutlineColourId, ui::controlOutlineColour());
    s.setColour(juce::Slider::trackColourId, ui::mutedTextColour());
    s.setColour(juce::Slider::thumbColourId, ui::textColour());
    s.setWantsKeyboardFocus(false);
}

bool isBlackKey(int pitch) {
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

const char* qualityName(Quality q) {
    switch (q) {
        case Quality::Major: return "Major";
        case Quality::Minor: return "Minor";
        case Quality::Diminished: return "Diminished";
        case Quality::Augmented: return "Augmented";
    }
    return "";
}

juce::String modeList(const Key& key, const ChordSpec& chord, bool& allInScale) {
    const auto mask = chordPitchClassMask(key, chord);
    allInScale = (mask & ~scalePitchClassMask(key)) == 0;
    juce::StringArray names;
    for (int m = 0; m < kParallelModeCount; ++m) {
        const auto mode = static_cast<ParallelMode>(m);
        std::uint16_t modeMask = 0;
        for (int s : parallelModeScale(mode)) modeMask = static_cast<std::uint16_t>(modeMask | (1u << ((key.tonic + s) % 12)));
        if ((mask & ~modeMask) == 0) names.add(toString(mode));
    }
    return names.joinIntoString(", ");
}

}  // namespace

// ---- PitchKeyboard -------------------------------------------------------------------------------------------------

void PitchKeyboard::setData(std::vector<Voice> v, std::vector<int> originalPitches, int low, int high) {
    voices = std::move(v);
    original = std::move(originalPitches);
    regLow = low;
    regHigh = high;
    repaint();
}

void PitchKeyboard::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds().toFloat();
    g.setColour(ui::controlFillColour());
    g.fillRoundedRectangle(bounds, 6.0f);

    // The window: the sounding notes plus a little room, in whole octaves, at least three octaves wide.
    int lo = regLow, hi = regHigh;
    if (!voices.empty() || !original.empty()) {
        lo = 127;
        hi = 0;
        for (const auto& v : voices) { lo = std::min(lo, v.pitch); hi = std::max(hi, v.pitch); }
        for (int p : original) { lo = std::min(lo, p); hi = std::max(hi, p); }
    }
    lo = std::max(0, lo - 5);
    hi = std::min(127, hi + 5);
    int start = (lo / 12) * 12;
    int end = std::min(127, ((hi / 12) + 1) * 12 - 1);
    while (end - start + 1 < 36) {
        if (end < 120) end += 12;
        else start = std::max(0, start - 12);
    }
    int whites = 0;
    for (int p = start; p <= end; ++p) if (!isBlackKey(p)) ++whites;
    if (whites == 0) return;

    const auto area = bounds.reduced(4.0f, 6.0f);
    const float keyW = area.getWidth() / static_cast<float>(whites);
    const float blackW = keyW * 0.62f, blackH = area.getHeight() * 0.58f;

    auto voiceAt = [this](int pitch) -> const Voice* {
        for (const auto& v : voices) if (v.pitch == pitch) return &v;
        return nullptr;
    };
    auto isOriginal = [this](int pitch) { return std::find(original.begin(), original.end(), pitch) != original.end(); };
    auto whiteX = [&](int pitch) {
        int index = 0;
        for (int p = start; p < pitch; ++p) if (!isBlackKey(p)) ++index;
        return area.getX() + keyW * static_cast<float>(index);
    };

    auto drawMarks = [&](int pitch, juce::Rectangle<float> key, bool black) {
        const Voice* v = voiceAt(pitch);
        const auto ink = black ? juce::Colours::white : juce::Colour(0xff111114);
        if (isOriginal(pitch)) {  // a ring: the original voicing's tone (hollow when it no longer sounds)
            g.setColour(v != nullptr ? ink : ui::mutedTextColour());
            g.drawEllipse(key.getCentreX() - 2.5f, key.getY() + 4.0f, 5.0f, 5.0f, 1.2f);
        }
        if (v != nullptr && v->doubling) {  // octave doubling
            g.setColour(ink);
            g.fillEllipse(key.getCentreX() - 2.0f, key.getBottom() - 10.0f, 4.0f, 4.0f);
        }
        if (v != nullptr && v->bass) {
            g.setColour(ink);
            g.setFont(ui::font(10.0f, true));
            g.drawText("B", key.withTrimmedTop(key.getHeight() * 0.35f).withHeight(12.0f), juce::Justification::centred);
        }
    };

    for (int p = start; p <= end; ++p) {  // white keys first
        if (isBlackKey(p)) continue;
        const auto key = juce::Rectangle<float>(whiteX(p), area.getY(), keyW, area.getHeight());
        const Voice* v = voiceAt(p);
        g.setColour(v != nullptr ? v->colour : juce::Colour(0xffd9d9dd));
        g.fillRect(key.reduced(0.5f, 0.0f));
        drawMarks(p, key, false);
    }
    for (int p = start; p <= end; ++p) {  // black keys on top, centred on the boundary after the previous white key
        if (!isBlackKey(p)) continue;
        const float x = whiteX(p) - blackW * 0.5f;
        const auto key = juce::Rectangle<float>(x, area.getY(), blackW, blackH);
        const Voice* v = voiceAt(p);
        g.setColour(v != nullptr ? v->colour.darker(0.15f) : juce::Colour(0xff26262b));
        g.fillRect(key);
        if (v != nullptr) {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.drawRect(key, 1.0f);
        }
        drawMarks(p, key, true);
    }

    // Register bounds: dim everything outside, with a bracket line at each edge.
    auto xFor = [&](int pitch) {
        int index = 0;
        for (int p = start; p < pitch; ++p) if (!isBlackKey(p)) ++index;
        return area.getX() + keyW * static_cast<float>(index);
    };
    g.setColour(juce::Colour(0xff0b0b0d).withAlpha(0.62f));
    if (regLow > start) g.fillRect(juce::Rectangle<float>(area.getX(), area.getY(), std::min(area.getWidth(), xFor(regLow) - area.getX()), area.getHeight()));
    if (regHigh < end) {
        const float x = xFor(regHigh + 1);
        g.fillRect(juce::Rectangle<float>(x, area.getY(), std::max(0.0f, area.getRight() - x), area.getHeight()));
    }
    g.setColour(ui::textColour());
    if (regLow >= start && regLow <= end) g.fillRect(xFor(regLow) - 1.0f, area.getY(), 2.0f, area.getHeight());
    if (regHigh >= start && regHigh <= end) g.fillRect(xFor(regHigh + 1) - 1.0f, area.getY(), 2.0f, area.getHeight());
}

// ---- HarmonicInspector ---------------------------------------------------------------------------------------------

HarmonicInspector::HarmonicInspector() {
    auto prepare = [this](juce::Button& b) {
        b.setWantsKeyboardFocus(false);
        addAndMakeVisible(b);
    };

    // Harmonic Definition editors
    static const char* const roots[12] = {"C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"};
    for (int pc = 0; pc < 12; ++pc) rootBox.addItem(roots[pc], pc + 1);
    qualityBox.addItem("Major", 1);
    qualityBox.addItem("Minor", 2);
    qualityBox.addItem("Diminished", 3);
    qualityBox.addItem("Augmented", 4);
    seventhBox.addItem("No 7th", 1);
    seventhBox.addItem("Minor 7th", 2);
    seventhBox.addItem("Major 7th", 3);
    seventhBox.addItem("Dim 7th", 4);
    rootBox.setTooltip("Root (absolute pitch class). Position and length stay as they are.");
    for (auto* b : {&rootBox, &qualityBox, &seventhBox}) {
        styleCombo(*b);
        addAndMakeVisible(*b);
    }
    rootBox.onChange = [this] {
        if (refreshing) return;
        const int pc = rootBox.getSelectedId() - 1;
        editChord("Chord root", [pc](const Progression& p, const ChordSpec& c) { return withRootPitchClass(p.key, c, pc); });
    };
    qualityBox.onChange = [this] {
        if (refreshing) return;
        static const Quality qualities[4] = {Quality::Major, Quality::Minor, Quality::Diminished, Quality::Augmented};
        const Quality q = qualities[std::clamp(qualityBox.getSelectedId() - 1, 0, 3)];
        editChord("Chord quality", [q](const Progression&, const ChordSpec& c) { return withQuality(c, q); });
    };
    seventhBox.onChange = [this] {
        if (refreshing) return;
        static const Seventh sevenths[4] = {Seventh::None, Seventh::Minor, Seventh::Major, Seventh::Diminished};
        const Seventh s = sevenths[std::clamp(seventhBox.getSelectedId() - 1, 0, 3)];
        editChord("Chord seventh", [s](const Progression&, const ChordSpec& c) { return withSeventhKind(c, s); });
    };
    ninthButton.onClick = [this] { editChord("Toggle 9th", [](const Progression& p, const ChordSpec& c) { return withNinthToggled(p.key.mode, c); }); };
    eleventhButton.onClick = [this] { editChord("Toggle 11th", [](const Progression& p, const ChordSpec& c) { return withEleventhToggled(p.key.mode, c); }); };
    thirteenthButton.onClick = [this] { editChord("Toggle 13th", [](const Progression& p, const ChordSpec& c) { return withThirteenthToggled(p.key.mode, c); }); };
    flat9Button.onClick = [this] { editChord("Toggle b9", [](const Progression& p, const ChordSpec& c) { return withAlterationToggled(p.key.mode, c, Alteration::FlatNine); }); };
    sharp9Button.onClick = [this] { editChord("Toggle #9", [](const Progression& p, const ChordSpec& c) { return withAlterationToggled(p.key.mode, c, Alteration::SharpNine); }); };
    sharp11Button.onClick = [this] { editChord("Toggle #11", [](const Progression& p, const ChordSpec& c) { return withAlterationToggled(p.key.mode, c, Alteration::SharpEleven); }); };
    flat13Button.onClick = [this] { editChord("Toggle b13", [](const Progression& p, const ChordSpec& c) { return withAlterationToggled(p.key.mode, c, Alteration::FlatThirteen); }); };
    sus2Button.onClick = [this] { editChord("Toggle sus2", [](const Progression&, const ChordSpec& c) { return withSuspensionToggled(c, Suspension::Sus2); }); };
    sus4Button.onClick = [this] { editChord("Toggle sus4", [](const Progression&, const ChordSpec& c) { return withSuspensionToggled(c, Suspension::Sus4); }); };
    inversionDown.onClick = [this] { editChord("Inversion down", [](const Progression&, const ChordSpec& c) { return withInversion(c, c.inversion - 1); }); };
    inversionUp.onClick = [this] { editChord("Inversion up", [](const Progression&, const ChordSpec& c) { return withInversion(c, c.inversion + 1); }); };
    for (auto* b : {&ninthButton, &eleventhButton, &thirteenthButton, &flat9Button, &sharp9Button, &sharp11Button, &flat13Button, &sus2Button,
                    &sus4Button, &inversionDown, &inversionUp}) prepare(*b);
    styleLabel(inversionLabel, 12.0f, false, ui::mutedTextColour(), juce::Justification::centred);
    addAndMakeVisible(inversionLabel);

    // Chromatic Dice
    styleLabel(diceCaption, 13.0f, true, ui::textColour());
    styleLabel(seedCaption, 12.0f, false, ui::mutedTextColour());
    styleLabel(diceResult, 11.5f, false, ui::mutedTextColour());
    diceResult.setMinimumHorizontalScale(1.0f);
    addAndMakeVisible(diceCaption);
    addAndMakeVisible(seedCaption);
    addAndMakeVisible(diceResult);
    diceModeBox.addItem("Preserve Root", 1);
    diceModeBox.addItem("Replace Harmony", 2);
    complexityBox.addItem("Conservative", 1);
    complexityBox.addItem("Adventurous", 2);
    complexityBox.addItem("Unrestricted", 3);
    sourceBox.addItem("Source: Any", 1);
    for (int m = 0; m < kParallelModeCount; ++m) sourceBox.addItem(toString(static_cast<ParallelMode>(m)), m + 2);
    diceModeBox.setTooltip("Preserve Root keeps the root note; Replace Harmony may pick any root from the parallel modes.");
    complexityBox.setTooltip("Conservative: common modes, simple chords. Adventurous: all nine modes and 9ths. Unrestricted: any root and quality.");
    for (auto* b : {&diceModeBox, &complexityBox, &sourceBox}) {
        styleCombo(*b);
        addAndMakeVisible(*b);
    }
    auto diceSetting = [this](const std::string& label, std::function<void(DiceSettings&)> change) {
        if (refreshing || !editProject) return;
        editProject(label, [change](Progression& p) { change(p.dice); }, "dice-settings");
    };
    diceModeBox.onChange = [this, diceSetting] {
        const auto mode = diceModeBox.getSelectedId() == 2 ? DiceMode::ReplaceHarmony : DiceMode::PreserveRoot;
        diceSetting("Dice mode", [mode](DiceSettings& d) { d.mode = mode; });
    };
    complexityBox.onChange = [this, diceSetting] {
        const auto c = static_cast<DiceComplexity>(std::clamp(complexityBox.getSelectedId() - 1, 0, 2));
        diceSetting("Dice complexity", [c](DiceSettings& d) { d.complexity = c; });
    };
    sourceBox.onChange = [this, diceSetting] {
        const auto f = static_cast<ModeFilter>(std::clamp(sourceBox.getSelectedId() - 1, 0, kParallelModeCount));
        diceSetting("Dice source mode", [f](DiceSettings& d) { d.sourceFilter = f; });
    };
    avoidButton.onClick = [this, diceSetting] {
        const bool on = !avoidButton.getToggleState();
        diceSetting("Dice avoid repeats", [on](DiceSettings& d) { d.avoidRepeats = on; });
    };
    rootLockButton.onClick = [this, diceSetting] {
        const bool on = !rootLockButton.getToggleState();
        diceSetting("Dice root lock", [on](DiceSettings& d) { d.rootLock = on; });
    };
    seedEditor.setInputRestrictions(10, "0123456789");
    seedEditor.setColour(juce::TextEditor::backgroundColourId, ui::controlFillColour());
    seedEditor.setColour(juce::TextEditor::textColourId, ui::textColour());
    seedEditor.setColour(juce::TextEditor::outlineColourId, ui::controlOutlineColour());
    seedEditor.setColour(juce::TextEditor::focusedOutlineColourId, ui::mutedTextColour());
    seedEditor.setFont(ui::font(13.0f));
    seedEditor.addListener(this);
    seedEditor.setTooltip("Deterministic seed: the same seed and roll count always give the same chord.");
    addAndMakeVisible(seedEditor);
    rollButton.onClick = [this] { rollDice(false); };
    rerollButton.onClick = [this] { rollDice(true); };
    diceUndoButton.onClick = [this] { if (onUndo) onUndo(); };
    regenButton.onClick = [this] { if (onRegenerateAffected) onRegenerateAffected(); };
    regenButton.setTooltip("Regenerates only the melody over chords that changed; locked notes stay.");
    for (auto* b : {&rollButton, &rerollButton, &diceUndoButton, &avoidButton, &rootLockButton, &regenButton}) prepare(*b);

    // Voice Allocation
    for (auto* l : {&densityCaption, &registerCaption, &distributionCaption, &customCaption}) {
        styleLabel(*l, 12.0f, false, ui::mutedTextColour());
        addAndMakeVisible(*l);
    }
    styleLabel(voicesLabel, 12.0f, false, ui::textColour());
    addAndMakeVisible(voicesLabel);
    densityBox.addItem("Auto / Original", 1);
    for (int n = 1; n <= kMaxDensity; ++n) densityBox.addItem(juce::String(n) + (n == 1 ? " voice" : " voices"), n + 1);
    registerBox.addItem("Low", 1);
    registerBox.addItem("Mid", 2);
    registerBox.addItem("High", 3);
    registerBox.addItem("Full", 4);
    registerBox.addItem("Custom", 5);
    distributionBox.addItem("Automatic", 1);
    distributionBox.addItem("Close", 2);
    distributionBox.addItem("Open", 3);
    distributionBox.addItem("Spread", 4);
    densityBox.setTooltip("Auto / Original plays the chord exactly as voiced. 1-12 voices reduce or expand it without changing the chord.");
    for (auto* b : {&densityBox, &registerBox, &distributionBox}) {
        styleCombo(*b);
        addAndMakeVisible(*b);
    }
    densityBox.onChange = [this] {
        const int n = densityBox.getSelectedId() - 1;
        changeVoicing("Chord density", [n](VoicingSettings& v) { v.density = n; });
    };
    registerBox.onChange = [this] {
        const auto r = static_cast<RegisterPreset>(std::clamp(registerBox.getSelectedId() - 1, 0, 4));
        changeVoicing("Voicing register", [r](VoicingSettings& v) { v.registerPreset = r; });
    };
    distributionBox.onChange = [this] {
        const auto d = static_cast<Distribution>(std::clamp(distributionBox.getSelectedId() - 1, 0, 3));
        changeVoicing("Voicing distribution", [d](VoicingSettings& v) { v.distribution = d; });
    };
    styleSlider(customLow, 0, 127, 36);
    styleSlider(customHigh, 0, 127, 84);
    customLow.onValueChange = [this] {
        const int lowValue = static_cast<int>(customLow.getValue());
        changeVoicing("Custom register", [lowValue](VoicingSettings& v) { v.customLow = lowValue; v.customHigh = std::max(v.customHigh, lowValue + 11); }, "voicing-custom");
    };
    customHigh.onValueChange = [this] {
        const int highValue = static_cast<int>(customHigh.getValue());
        changeVoicing("Custom register", [highValue](VoicingSettings& v) { v.customHigh = highValue; v.customLow = std::min(v.customLow, highValue - 11); }, "voicing-custom");
    };
    addAndMakeVisible(customLow);
    addAndMakeVisible(customHigh);
    preserveBassButton.onClick = [this] {
        const bool on = !preserveBassButton.getToggleState();
        changeVoicing("Preserve bass", [on](VoicingSettings& v) { v.preserveBass = on; });
    };
    voiceLeadingButton.onClick = [this] {
        const bool on = !voiceLeadingButton.getToggleState();
        changeVoicing("Voice leading", [on](VoicingSettings& v) { v.voiceLeading = on; });
    };
    globalScopeButton.onClick = [this] { editingSelectedScope = false; rebuild(); };
    selectedScopeButton.onClick = [this] { editingSelectedScope = true; rebuild(); };
    useGlobalButton.onClick = [this] {
        if (!editSelected) return;
        editSelected("Use global voicing", [](Progression& p, std::size_t i) { p.blocks.at(i).voicing.reset(); });
    };
    for (auto* b : {&globalScopeButton, &selectedScopeButton, &useGlobalButton, &preserveBassButton, &voiceLeadingButton}) prepare(*b);
    addAndMakeVisible(keyboard);
    setSize(kWidth, kContentHeight);
}

VoicingSettings HarmonicInspector::shownVoicing() const {
    if (editingSelectedScope && primary >= 0 && static_cast<std::size_t>(primary) < model.blocks.size()) return model.voicingFor(static_cast<std::size_t>(primary));
    return model.voicing;
}

void HarmonicInspector::setModel(const Progression& m, const std::vector<std::size_t>& sel, int prim) {
    model = m;
    selection = sel;
    selection.erase(std::remove_if(selection.begin(), selection.end(), [this](std::size_t i) { return i >= model.blocks.size(); }), selection.end());
    primary = (prim >= 0 && static_cast<std::size_t>(prim) < model.blocks.size()) ? prim : (selection.empty() ? -1 : static_cast<int>(selection.back()));
    if (selection.empty()) editingSelectedScope = false;
    rebuild();
}

std::vector<HarmonicInspector::Row> HarmonicInspector::infoFor(const Progression& p, const ChordSpec& chord) const {
    std::vector<Row> out;
    const auto roman = formatRomanNumeral(chord);
    bool allInScale = false;
    const juce::String modes = modeList(p.key, chord, allInScale);
    const int root = chordRootPitchClass(p.key, chord);
    const auto intervals = chordIntervals(chord);
    const int bassPc = (root + intervals[static_cast<std::size_t>(std::min<int>(chord.inversion, static_cast<int>(intervals.size()) - 1))]) % 12;
    juce::StringArray alterations;
    if (chord.ninth == Ninth::Minor) alterations.add("b9");
    if (chord.ninth == Ninth::Sharp) alterations.add("#9");
    if (chord.eleventh == Eleventh::Sharp) alterations.add("#11");
    if (chord.thirteenth == Thirteenth::Minor) alterations.add("b13");
    const auto extensions = extensionSymbol(chord);
    static const char* const inversionNames[5] = {"Root position", "1st inversion", "2nd inversion", "3rd inversion", "4th inversion"};
    juce::String interpretation = allInScale ? "Diatonic" : (modes.isEmpty() ? "Chromatic" : "Borrowed");
    out.push_back({"Chord", juce::String(chordName(p.key, chord))});
    out.push_back({"Roman numeral", roman ? ui::displayNumeral(juce::String(*roman)) : juce::String("-")});
    out.push_back({"Root", juce::String(noteName(p.key, root))});
    out.push_back({"Quality", qualityName(chord.quality)});
    out.push_back({"Extensions", extensions.empty() ? juce::String("none") : juce::String(extensions)});
    out.push_back({"Alterations / sus", (alterations.isEmpty() ? juce::String("none") : alterations.joinIntoString(", ")) +
                                            (chord.suspension == Suspension::Sus2 ? ", sus2" : chord.suspension == Suspension::Sus4 ? ", sus4" : "")});
    out.push_back({"Inversion / bass", juce::String(inversionNames[std::clamp(chord.inversion, 0, 4)]) + ", bass " + juce::String(noteName(p.key, bassPc))});
    out.push_back({"Project", juce::String(noteName(p.key, p.key.tonic)) + (p.key.mode == Mode::Major ? " major" : " minor")});
    out.push_back({"Source modes", modes.isEmpty() ? juce::String("none (chromatic)") : modes});
    out.push_back({"Interpretation", interpretation});
    return out;
}

void HarmonicInspector::rebuild() {
    refreshing = true;
    rows.clear();
    const bool have = !selection.empty() && primary >= 0;
    emptyText = have ? juce::String() : juce::String("No chord selected. Select a chord on the timeline to inspect and edit it.");

    if (have) {
        rows = infoFor(model, model.blocks[static_cast<std::size_t>(primary)].chord);
        if (selection.size() > 1) {  // shared value, or "Mixed"
            for (std::size_t r = 0; r < rows.size(); ++r) {
                for (std::size_t i : selection) {
                    if (infoFor(model, model.blocks[i].chord)[r].value != rows[r].value) { rows[r].value = "Mixed"; break; }
                }
            }
        }
    }
    const ChordSpec chord = have ? model.blocks[static_cast<std::size_t>(primary)].chord : ChordSpec{};
    auto same = [&](auto get) {
        for (std::size_t i : selection) if (get(model.blocks[i].chord) != get(chord)) return false;
        return true;
    };
    for (auto* b : {&rootBox, &qualityBox, &seventhBox}) b->setEnabled(have);
    if (have && same([&](const ChordSpec& c) { return chordRootPitchClass(model.key, c); })) rootBox.setSelectedId(chordRootPitchClass(model.key, chord) + 1, juce::dontSendNotification);
    else rootBox.setText(have ? "Mixed" : "", juce::dontSendNotification);
    if (have && same([](const ChordSpec& c) { return static_cast<int>(c.quality); })) qualityBox.setSelectedId(static_cast<int>(chord.quality) + 1, juce::dontSendNotification);
    else qualityBox.setText(have ? "Mixed" : "", juce::dontSendNotification);
    if (have && same([](const ChordSpec& c) { return static_cast<int>(c.seventh); })) seventhBox.setSelectedId(static_cast<int>(chord.seventh) + 1, juce::dontSendNotification);
    else seventhBox.setText(have ? "Mixed" : "", juce::dontSendNotification);
    auto pill = [&](ui::StyledButton& b, bool on) {
        b.setEnabled(have);
        b.setToggleState(have && on, juce::dontSendNotification);
    };
    pill(ninthButton, chord.ninth != Ninth::None);
    pill(eleventhButton, chord.eleventh != Eleventh::None);
    pill(thirteenthButton, chord.thirteenth != Thirteenth::None);
    pill(flat9Button, chord.ninth == Ninth::Minor);
    pill(sharp9Button, chord.ninth == Ninth::Sharp);
    pill(sharp11Button, chord.eleventh == Eleventh::Sharp);
    pill(flat13Button, chord.thirteenth == Thirteenth::Minor);
    pill(sus2Button, chord.suspension == Suspension::Sus2);
    pill(sus4Button, chord.suspension == Suspension::Sus4);
    inversionDown.setEnabled(have && chord.inversion > 0);
    inversionUp.setEnabled(have && chord.inversion < maxInversion(chord));
    inversionLabel.setText(have ? juce::String(chord.inversion == 0 ? "Root pos." : juce::String(chord.inversion) + (chord.inversion == 1 ? "st inv." : chord.inversion == 2 ? "nd inv." : "rd inv.")) : juce::String("Inversion"),
                           juce::dontSendNotification);

    // Dice
    const bool single = selection.size() == 1;
    for (auto* b : {&rollButton, &rerollButton}) b->setEnabled(single);
    diceUndoButton.setEnabled(true);
    diceModeBox.setSelectedId(model.dice.mode == DiceMode::ReplaceHarmony ? 2 : 1, juce::dontSendNotification);
    complexityBox.setSelectedId(static_cast<int>(model.dice.complexity) + 1, juce::dontSendNotification);
    sourceBox.setSelectedId(static_cast<int>(model.dice.sourceFilter) + 1, juce::dontSendNotification);
    avoidButton.setToggleState(model.dice.avoidRepeats, juce::dontSendNotification);
    rootLockButton.setToggleState(model.dice.rootLock, juce::dontSendNotification);
    if (!seedEditor.hasKeyboardFocus(true)) seedEditor.setText(juce::String(static_cast<juce::int64>(model.dice.seed)), juce::dontSendNotification);
    diceResult.setText(!selection.empty() && !single ? juce::String("Dice rolls one chord at a time: select a single chord.") : diceMessage, juce::dontSendNotification);
    regenButton.setEnabled(!affectedRanges(model).empty());

    // Voice allocation
    const VoicingSettings v = shownVoicing();
    const bool selectedScope = editingSelectedScope && have;
    globalScopeButton.setToggleState(!selectedScope, juce::dontSendNotification);
    selectedScopeButton.setToggleState(selectedScope, juce::dontSendNotification);
    selectedScopeButton.setEnabled(have);
    bool anyOverride = false, mixedVoicing = false;
    if (selectedScope) {
        for (std::size_t i : selection) {
            anyOverride = anyOverride || model.blocks[i].voicing.has_value();
            mixedVoicing = mixedVoicing || model.voicingFor(i) != v;
        }
    }
    useGlobalButton.setEnabled(selectedScope && anyOverride);
    if (mixedVoicing) {
        densityBox.setText("Mixed", juce::dontSendNotification);
    } else {
        densityBox.setSelectedId(v.density + 1, juce::dontSendNotification);
    }
    registerBox.setSelectedId(static_cast<int>(v.registerPreset) + 1, juce::dontSendNotification);
    distributionBox.setSelectedId(static_cast<int>(v.distribution) + 1, juce::dontSendNotification);
    preserveBassButton.setToggleState(v.preserveBass, juce::dontSendNotification);
    voiceLeadingButton.setToggleState(v.voiceLeading, juce::dontSendNotification);
    customLow.setValue(v.customLow, juce::dontSendNotification);
    customHigh.setValue(v.customHigh, juce::dontSendNotification);
    const bool custom = v.registerPreset == RegisterPreset::Custom;
    customLow.setEnabled(custom);
    customHigh.setEnabled(custom);
    customCaption.setText(custom ? "Custom low / high" : "Custom (choose Register: Custom)", juce::dontSendNotification);

    // Keyboard + target vs actual (the primary chord, as it will play)
    std::vector<PitchKeyboard::Voice> keys;
    std::vector<int> originalPitches;
    int low = 0, high = 127;
    voiceSummary = "Select a chord to see its voices.";
    if (have) {
        try {
            const auto voiced = voiceProgression(model);
            const auto originals = voiceProgression(model, VoicingOptions{}, true);
            const auto& vc = voiced[static_cast<std::size_t>(primary)];
            HarmonicSettings hs;
            hs.mapping = model.melodySettings.mapping;
            hs.palette = model.melodySettings.palette;
            const HarmonyResolver resolver(model.key, hs);
            for (const auto& n : vc.notes) {
                const auto c = resolver.identity(n.pitch).colour;
                keys.push_back({n.pitch, n.role == VoiceRole::Bass, n.doubling, juce::Colour(c.r, c.g, c.b)});
            }
            originalPitches = originals[static_cast<std::size_t>(primary)].pitches();
            low = vc.range.low;
            high = vc.range.high;
            const juce::String registerText = vc.original ? juce::String("original voicing") : "register " + juce::String(vc.range.low) + "-" + juce::String(vc.range.high);
            voiceSummary = "Voices: target " + juce::String(vc.targetVoices) + " / actual " + juce::String(vc.actualVoices) + "  (" + registerText + ")";
            if (!vc.note.empty()) voiceSummary += "\n" + juce::String(vc.note);
            if (selection.size() > 1) voiceSummary += "\nShowing the primary of " + juce::String(static_cast<int>(selection.size())) + " selected chords.";
        } catch (const std::exception&) {
            voiceSummary = "This chord cannot be voiced here.";
        }
    }
    keyboard.setData(std::move(keys), std::move(originalPitches), low, high);
    voicesLabel.setText(voiceSummary, juce::dontSendNotification);
    refreshing = false;
    repaint();
}

void HarmonicInspector::editChord(const std::string& label, const std::function<ChordSpec(const Progression&, const ChordSpec&)>& change) {
    if (refreshing || !editSelected) return;
    editSelected(label, [change](Progression& p, std::size_t i) {
        const ChordSpec before = p.blocks.at(i).chord;
        p.blocks.at(i).chord = change(p, before);  // position, length, octave, density and melody are not touched
    });
}

void HarmonicInspector::changeVoicing(const std::string& label, const std::function<void(VoicingSettings&)>& change, const std::string& key) {
    if (refreshing) return;
    if (editingSelectedScope && !selection.empty() && editSelected) {
        editSelected(label, [change](Progression& p, std::size_t i) {
            VoicingSettings s = p.voicingFor(i);
            change(s);
            p.blocks.at(i).voicing = s;
        });
    } else if (editProject) {
        editProject(label, [change](Progression& p) { change(p.voicing); }, key);
    }
}

void HarmonicInspector::rollDice(bool reroll) {
    if (!editProject || selection.size() != 1) return;
    const std::size_t index = selection.front();
    if (index >= model.blocks.size()) return;
    DiceResult result;
    const ChordSpec before = model.blocks[index].chord;
    const std::vector<ChordSpec> recent = recentRolls;
    editProject(reroll ? "Reroll chromatic dice" : "Chromatic dice", [&](Progression& p) {
        result = rollChromaticDice(p, index, p.dice, &recent);
        if (!result.ok) return;  // nothing eligible: the chord stays, the message says why
        p.blocks.at(index).chord = result.chord;  // the concrete chord is stored, so Redo restores exactly this result
        p.dice.rollIndex += 1;
    }, {});
    diceMessage = juce::String(result.message);
    if (result.ok) {
        recentRolls.push_back(before);
        if (recentRolls.size() > 4) recentRolls.erase(recentRolls.begin());
        if (onMessage) onMessage("Chromatic Dice: " + juce::String(result.message));
    } else if (onMessage) {
        onMessage(juce::String(result.message));
    }
    diceResult.setText(diceMessage, juce::dontSendNotification);
}

void HarmonicInspector::textEditorReturnKeyPressed(juce::TextEditor&) { commitSeed(); }
void HarmonicInspector::textEditorFocusLost(juce::TextEditor&) { commitSeed(); }

void HarmonicInspector::commitSeed() {
    if (refreshing || !editProject) return;
    const auto value = seedEditor.getText().getLargeIntValue();
    if (value < 0 || value > 4294967295LL || value == static_cast<juce::int64>(model.dice.seed)) return;
    editProject("Dice seed", [value](Progression& p) {
        p.dice.seed = static_cast<std::uint32_t>(value);
        p.dice.rollIndex = 0;  // a new seed starts its sequence again
    }, {});
}

void HarmonicInspector::paint(juce::Graphics& g) {
    auto card = [&g](juce::Rectangle<int> r, const juce::String& title) {
        g.setColour(ui::panelBorderColour());
        g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 10.0f, 1.0f);
        g.setColour(ui::textColour());
        g.setFont(ui::font(12.0f, true));
        g.drawText(title, r.getX() + 12, r.getY() + 6, r.getWidth() - 24, 20, juce::Justification::centredLeft);
    };
    card(section1, "HARMONIC DEFINITION");
    card(section2, "VOICE ALLOCATION");
    card(section3, "PERFORMANCE");

    g.setFont(ui::font(12.0f));
    if (rows.empty()) {
        g.setColour(ui::mutedTextColour());
        g.drawFittedText(emptyText, infoArea, juce::Justification::topLeft, 3);
    } else {
        int y = infoArea.getY();
        for (const auto& r : rows) {
            g.setColour(ui::mutedTextColour());
            g.drawText(r.name, infoArea.getX(), y, 104, 16, juce::Justification::centredLeft);
            g.setColour(ui::textColour());
            g.drawFittedText(r.value, infoArea.getX() + 106, y, infoArea.getWidth() - 106, 16, juce::Justification::centredLeft, 1);
            y += 17;
        }
    }

    // Performance: reserved, intentionally without controls.
    g.setColour(ui::mutedTextColour());
    g.setFont(ui::font(11.5f));
    g.drawFittedText("Reserved for CHORD/GATE (not available yet). The signal path is:\nHarmonic Definition > Voice Allocation > CHORD/GATE > MIDI playback / export.",
                     section3.reduced(12, 0).withTrimmedTop(28).withTrimmedBottom(6), juce::Justification::topLeft, 4);
}

void HarmonicInspector::resized() {
    auto area = getLocalBounds().reduced(2, 2);
    const int pad = 12;
    auto inner = [&](juce::Rectangle<int> card) { return card.reduced(pad, 0).withTrimmedTop(28); };

    // ---- Harmonic Definition
    section1 = area.removeFromTop(540);
    auto s1 = inner(section1);
    infoArea = s1.removeFromTop(10 * 17 + 4);
    s1.removeFromTop(4);
    auto row = s1.removeFromTop(28);
    const int third = (row.getWidth() - 12) / 3;
    rootBox.setBounds(row.removeFromLeft(third));
    row.removeFromLeft(6);
    qualityBox.setBounds(row.removeFromLeft(third + 6));
    row.removeFromLeft(6);
    seventhBox.setBounds(row);
    s1.removeFromTop(6);
    row = s1.removeFromTop(24);
    const int pillW = (row.getWidth() - 6 * 5) / 7;
    for (auto* b : {&ninthButton, &eleventhButton, &thirteenthButton, &flat9Button, &sharp9Button, &sharp11Button, &flat13Button}) {
        b->setBounds(row.removeFromLeft(pillW));
        row.removeFromLeft(5);
    }
    s1.removeFromTop(6);
    row = s1.removeFromTop(24);
    sus2Button.setBounds(row.removeFromLeft(48));
    row.removeFromLeft(5);
    sus4Button.setBounds(row.removeFromLeft(48));
    row.removeFromLeft(10);
    inversionUp.setBounds(row.removeFromRight(26));
    inversionDown.setBounds(row.removeFromLeft(26));
    inversionLabel.setBounds(row);
    s1.removeFromTop(12);

    diceCaption.setBounds(s1.removeFromTop(20));
    s1.removeFromTop(2);
    row = s1.removeFromTop(30);
    rollButton.setBounds(row.removeFromLeft(100));
    row.removeFromLeft(6);
    rerollButton.setBounds(row.removeFromLeft(72));
    row.removeFromLeft(6);
    diceUndoButton.setBounds(row.removeFromLeft(60));
    s1.removeFromTop(6);
    row = s1.removeFromTop(26);
    diceModeBox.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2));
    row.removeFromLeft(6);
    complexityBox.setBounds(row);
    s1.removeFromTop(6);
    row = s1.removeFromTop(26);
    sourceBox.setBounds(row.removeFromLeft(124));
    row.removeFromLeft(6);
    avoidButton.setBounds(row.removeFromLeft(78));
    row.removeFromLeft(5);
    rootLockButton.setBounds(row);
    s1.removeFromTop(6);
    row = s1.removeFromTop(24);
    seedCaption.setBounds(row.removeFromLeft(36));
    seedEditor.setBounds(row.removeFromLeft(110));
    s1.removeFromTop(4);
    diceResult.setBounds(s1.removeFromTop(48));
    s1.removeFromTop(2);
    regenButton.setBounds(s1.removeFromTop(26));

    area.removeFromTop(10);
    // ---- Voice Allocation
    section2 = area.removeFromTop(372);
    auto s2 = inner(section2);
    row = s2.removeFromTop(24);
    globalScopeButton.setBounds(row.removeFromLeft(64));
    row.removeFromLeft(5);
    selectedScopeButton.setBounds(row.removeFromLeft(110));
    row.removeFromLeft(5);
    useGlobalButton.setBounds(row);
    s2.removeFromTop(8);
    row = s2.removeFromTop(16);
    densityCaption.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2));
    row.removeFromLeft(6);
    registerCaption.setBounds(row);
    row = s2.removeFromTop(28);
    densityBox.setBounds(row.removeFromLeft((row.getWidth() - 6) / 2));
    row.removeFromLeft(6);
    registerBox.setBounds(row);
    s2.removeFromTop(6);
    row = s2.removeFromTop(16);
    distributionCaption.setBounds(row);
    row = s2.removeFromTop(28);
    distributionBox.setBounds(row.removeFromLeft(124));
    row.removeFromLeft(6);
    preserveBassButton.setBounds(row.removeFromLeft(92));
    row.removeFromLeft(5);
    voiceLeadingButton.setBounds(row);
    s2.removeFromTop(6);
    customCaption.setBounds(s2.removeFromTop(16));
    customLow.setBounds(s2.removeFromTop(22));
    customHigh.setBounds(s2.removeFromTop(22));
    s2.removeFromTop(6);
    voicesLabel.setBounds(s2.removeFromTop(48));
    s2.removeFromTop(4);
    keyboard.setBounds(s2.removeFromTop(92));

    area.removeFromTop(10);
    section3 = area.removeFromTop(92);
}
