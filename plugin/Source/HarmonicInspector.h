#pragma once

#include "UiKit.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <chordcore/ChromaticDice.h>
#include <chordcore/Document.h>
#include <chordcore/Progression.h>
#include <chordcore/Voicing.h>

#include <functional>
#include <vector>

/// MIDI pitch preview for the Voice Allocation section: a piano keyboard on which every sounding pitch is filled with
/// the central harmonic colour (HarmonyResolver), the bass is marked B, octave doublings carry a dot, tones of the
/// original voicing carry a ring (hollow = no longer sounding), and everything outside the register is dimmed.
class PitchKeyboard final : public juce::Component {
public:
    struct Voice {
        int pitch = 60;
        bool bass = false;
        bool doubling = false;
        juce::Colour colour;
    };

    void setData(std::vector<Voice> voices, std::vector<int> originalPitches, int registerLow, int registerHigh);
    void paint(juce::Graphics&) override;

private:
    std::vector<Voice> voices;
    std::vector<int> original;
    int regLow = 0, regHigh = 127;
};

/// The Harmonic Inspector (CHORD / DEFINE): one persistent panel beside the timeline with three sections.
///   HARMONIC DEFINITION  what the selected chord is (name, numeral, root, quality, extensions, inversion, bass, source modes,
///                        interpretation), the chord editors, and Chromatic Dice.
///   VOICE ALLOCATION     Chord Density: voices, register, distribution, Preserve Bass, Voice Leading, global or per chord,
///                        target vs actual voices, and the pitch preview keyboard.
///   PERFORMANCE          reserved for the future CHORD/GATE stage; it has no controls.
/// Pipeline order: Harmonic Definition -> Voice Allocation -> CHORD/GATE -> MIDI playback / export.
///
/// The inspector holds no chord state of its own: it reads the project model it is given and writes back through the
/// two edit callbacks (undoable, validated, the same path as the rest of the editor). No music rules live here.
class HarmonicInspector final : public juce::Component, private juce::TextEditor::Listener {
public:
    static constexpr int kWidth = 304;
    static constexpr int kContentHeight = 1030;

    HarmonicInspector();

    /// `selection` = selected block indices (ascending), `primary` = the one the info shows (-1 when none).
    void setModel(const chordcore::Progression& model, const std::vector<std::size_t>& selection, int primary);

    /// Edits every selected chord inside ONE undoable step.
    std::function<void(const std::string& label, const std::function<void(chordcore::Progression&, std::size_t)>&)> editSelected;
    /// Edits the project (global settings, dice settings, a single block).
    std::function<void(const std::string& label, const std::function<void(chordcore::Progression&)>&, const std::string& coalesceKey)> editProject;
    std::function<void()> onUndo, onRegenerateAffected;
    std::function<void(const juce::String&)> onMessage;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Row {
        juce::String name, value;
    };

    void rebuild();
    std::vector<Row> infoFor(const chordcore::Progression& p, const chordcore::ChordSpec& chord) const;
    chordcore::VoicingSettings shownVoicing() const;
    void changeVoicing(const std::string& label, const std::function<void(chordcore::VoicingSettings&)>& change, const std::string& key = {});
    void rollDice(bool reroll);
    void textEditorReturnKeyPressed(juce::TextEditor&) override;
    void textEditorFocusLost(juce::TextEditor&) override;
    void commitSeed();
    void editChord(const std::string& label, const std::function<chordcore::ChordSpec(const chordcore::Progression&, const chordcore::ChordSpec&)>& change);

    chordcore::Progression model;
    std::vector<std::size_t> selection;
    int primary = -1;
    bool refreshing = false;
    bool editingSelectedScope = false;  // Voice Allocation: false = global settings, true = the selected chords
    std::vector<chordcore::ChordSpec> recentRolls;
    juce::String diceMessage;
    std::vector<Row> rows;
    juce::String voiceSummary, emptyText;

    juce::Rectangle<int> section1, section2, section3, infoArea;

    // Harmonic Definition editors
    juce::ComboBox rootBox, qualityBox, seventhBox;
    ui::StyledButton ninthButton{"9", ui::StyledButton::Look::Pill, 12.0f}, eleventhButton{"11", ui::StyledButton::Look::Pill, 12.0f},
        thirteenthButton{"13", ui::StyledButton::Look::Pill, 12.0f}, flat9Button{"b9", ui::StyledButton::Look::Pill, 12.0f},
        sharp9Button{"#9", ui::StyledButton::Look::Pill, 12.0f}, sharp11Button{"#11", ui::StyledButton::Look::Pill, 12.0f},
        flat13Button{"b13", ui::StyledButton::Look::Pill, 12.0f}, sus2Button{"sus2", ui::StyledButton::Look::Pill, 12.0f},
        sus4Button{"sus4", ui::StyledButton::Look::Pill, 12.0f}, inversionDown{"-", ui::StyledButton::Look::Outline, 14.0f},
        inversionUp{"+", ui::StyledButton::Look::Outline, 14.0f};
    juce::Label inversionLabel;

    // Chromatic Dice
    juce::Label diceCaption{{}, "Chromatic Dice"}, seedCaption{{}, "Seed"}, diceResult;
    ui::StyledButton rollButton{"Dice Roll", ui::StyledButton::Look::Primary, 13.0f}, rerollButton{"Reroll", ui::StyledButton::Look::Outline, 13.0f},
        diceUndoButton{"Undo", ui::StyledButton::Look::Outline, 13.0f}, avoidButton{"Avoid repeats", ui::StyledButton::Look::Pill, 11.0f},
        rootLockButton{"Root lock", ui::StyledButton::Look::Pill, 11.0f}, regenButton{"Regenerate Affected Melody", ui::StyledButton::Look::Outline, 12.0f};
    juce::ComboBox diceModeBox, complexityBox, sourceBox;
    juce::TextEditor seedEditor;

    // Voice Allocation
    ui::StyledButton globalScopeButton{"Global", ui::StyledButton::Look::Pill, 12.0f}, selectedScopeButton{"Selected chords", ui::StyledButton::Look::Pill, 12.0f},
        useGlobalButton{"Use global", ui::StyledButton::Look::Outline, 12.0f}, preserveBassButton{"Preserve Bass", ui::StyledButton::Look::Pill, 11.0f},
        voiceLeadingButton{"Voice Leading", ui::StyledButton::Look::Pill, 11.0f};
    juce::Label densityCaption{{}, "Density"}, registerCaption{{}, "Register"}, distributionCaption{{}, "Layout"}, customCaption{{}, "Custom"},
        voicesLabel;
    juce::ComboBox densityBox, registerBox, distributionBox;
    juce::Slider customLow, customHigh;
    PitchKeyboard keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicInspector)
};
