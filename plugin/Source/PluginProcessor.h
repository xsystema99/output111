#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <chordcore/Progression.h>
#include <chordcore/Scheduler.h>

/// Thin JUCE adapter around chordcore. No music logic lives here.
///
/// Scope so far: holds the demo progression, exposes MIDI-file export (milestone 1) and plays the
/// progression as MIDI output in sync with the host transport (milestone 3). It does not yet persist
/// state (milestone 4).
///
/// Threading: the progression and the scheduler's note list are built once in the constructor and
/// only read afterwards. When editing arrives (milestones 4 and 6) the note list will have to be
/// swapped into the audio thread safely; do not mutate it from the message thread before then.
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
    bool acceptsMidi() const override { return true; }  // Live requires an event input bus on instruments
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
    chordcore::PlaybackScheduler scheduler;
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordSketchProcessor)
};
