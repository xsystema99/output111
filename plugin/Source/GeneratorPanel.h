#pragma once

#include "PluginProcessor.h"
#include "UiKit.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <chordcore/Melody.h>

#include <functional>

/// The melody generator's controls, shown in a call-out box from the "Generate..." button. Setting changes are
/// ordinary undoable project edits (they are saved with the project); the buttons call back into the editor,
/// which runs the generator (chordcore::regenerateMelody) as ONE undoable edit on the message thread.
class GeneratorPanel final : public juce::Component {
public:
    explicit GeneratorPanel(ChordSketchProcessor&);

    void refresh();  ///< re-read the settings from the project (e.g. after a Reroll changed the seed)

    std::function<void()> onGenerate, onReroll, onSelectedRegion, onWholeProject, onAffected;

    void resized() override;
    void paint(juce::Graphics&) override;

private:
    void settingChanged();
    void seedEntered();

    ChordSketchProcessor& chordProcessor;
    bool refreshing = false;

    juce::Label contourCaption{{}, "Contour"}, densityCaption{{}, "Density"}, rhythmCaption{{}, "Rhythm"}, scopeCaption{{}, "Length"},
        tensionCaption{{}, "Tension"}, lowCaption{{}, "Lowest"}, highCaption{{}, "Highest"}, seedCaption{{}, "Seed"};
    juce::ComboBox contourBox, densityBox, rhythmBox, scopeBox;
    juce::Slider tensionSlider, lowSlider, highSlider;
    juce::TextEditor seedEditor;
    ui::StyledButton generateButton{"Generate", ui::StyledButton::Look::Primary, 14.0f};
    ui::StyledButton rerollButton{"Reroll", ui::StyledButton::Look::Outline, 13.0f};
    ui::StyledButton selectedButton{"Selected region", ui::StyledButton::Look::Outline, 12.0f};
    ui::StyledButton wholeButton{"Whole project", ui::StyledButton::Look::Outline, 12.0f};
    ui::StyledButton affectedButton{"Regenerate affected", ui::StyledButton::Look::Outline, 12.0f};
    juce::Label hint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneratorPanel)
};
