#pragma once

#include "PluginProcessor.h"
#include "TimelineComponent.h"
#include "UiKit.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <chordcore/Progression.h>

#include <functional>
#include <memory>
#include <vector>

/// Milestone 6 editor, laid out after the supplied mockup:
///
///   header           title, VST3 badge
///   Key / Scale / Meter
///   Chord timeline   coloured chord cards (TimelineComponent)
///   Chord palette    the seven diatonic chords of the current scale; click one to set the selected chord
///   Selected chord   Triad / 7th / 9th / Sus2 / Sus4, plus Split / Duplicate / Delete
///   tools            Undo, Redo, grid snap, tempo, Export MIDI
///   footer           Copy / Paste progression as text ("I V vi IV")
///
/// Everything it does to the project goes through ChordSketchProcessor::editProgression (undoable), using
/// the rules in chordcore (ChordEdit.h, Timeline.h). No music logic lives here.
class ChordSketchEditor final : public juce::AudioProcessorEditor, private juce::ChangeListener {
public:
    explicit ChordSketchEditor(ChordSketchProcessor&);
    ~ChordSketchEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshFromModel();
    void setStatus(const juce::String& text);

    void selectionChanged(int index);
    void applyToSelected(const std::string& label, const std::function<void(chordcore::Progression&, std::size_t)>& change);
    void paletteClicked(int degree);
    void keyChanged();
    void scaleChanged();
    void tempoChanged();
    void splitClicked();
    void duplicateClicked();
    void deleteClicked();
    void copyClicked();
    void pasteClicked();
    void exportMidiClicked();
    void writeMidiTo(const juce::File& file);

    ChordSketchProcessor& chordProcessor;
    chordcore::Progression model;  // last value read from the chordProcessor
    int selected = -1;             // index of the selected block, -1 = none

    // header
    juce::Rectangle<int> iconBounds, badgeBounds;
    juce::Label titleLabel, subtitleLabel;

    // key / scale / meter
    juce::Label keyCaption{{}, "Key"}, scaleCaption{{}, "Scale"}, meterCaption{{}, "Meter"}, meterValue;
    juce::ComboBox tonicBox, scaleBox;

    // timeline
    juce::Label timelineCaption{{}, "Chord timeline"}, timelineHint;
    TimelineComponent timeline;

    // palette
    juce::Label paletteCaption{{}, "Chord palette"};
    std::vector<std::unique_ptr<ui::StyledButton>> paletteButtons;

    // selected chord
    juce::Label selectedCaption{{}, "Selected chord"}, selectedName;
    ui::StyledButton triadButton{"Triad", ui::StyledButton::Look::Pill};
    ui::StyledButton seventhButton{"7th", ui::StyledButton::Look::Pill};
    ui::StyledButton ninthButton{"9th", ui::StyledButton::Look::Pill};
    ui::StyledButton sus2Button{"Sus2", ui::StyledButton::Look::Pill};
    ui::StyledButton sus4Button{"Sus4", ui::StyledButton::Look::Pill};
    ui::StyledButton splitButton{"Split", ui::StyledButton::Look::Outline, 13.0f};
    ui::StyledButton duplicateButton{"Duplicate", ui::StyledButton::Look::Outline, 13.0f};
    ui::StyledButton deleteButton{"Delete", ui::StyledButton::Look::Outline, 13.0f};

    // tools
    ui::StyledButton undoButton{"Undo", ui::StyledButton::Look::Outline};
    ui::StyledButton redoButton{"Redo", ui::StyledButton::Look::Outline};
    juce::ComboBox snapBox;
    juce::Slider tempoSlider;
    ui::StyledButton exportButton{"Export MIDI...", ui::StyledButton::Look::Primary, 15.0f};

    // footer
    juce::Label footerNote;
    ui::StyledButton copyButton{"Copy progression", ui::StyledButton::Look::Outline};
    ui::StyledButton pasteButton{"Paste", ui::StyledButton::Look::Outline};
    juce::Label statusLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;  // must outlive the async dialog

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordSketchEditor)
};
