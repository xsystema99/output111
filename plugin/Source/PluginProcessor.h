#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <chordcore/Progression.h>

/// Thin JUCE adapter around chordcore. No music logic lives here.
///
/// Milestone 1 scope: holds the demo progression and exposes MIDI-file export. It does NOT yet
/// emit MIDI from processBlock (transport-synchronised playback is milestone 3) and does not yet
/// persist state (milestone 4).
class ChordSketchProcessor final : public juce::AudioProcessor {
public:
    ChordSketchProcessor();
    ~ChordSketchProcessor() override = default;

    // ---- chordcore access (message thread only in M1: the model is never mutated after construction)
    const chordcore::Progression& getProgression() const noexcept { return progression; }

    /// Builds the Standard MIDI File for the current progression via chordcore's single export path.
    /// Throws std::exception on failure.
    std::vector<std::uint8_t> exportMidiBytes() const;

    // ---- juce::AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ChordSketch"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    chordcore::Progression progression;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordSketchProcessor)
};
