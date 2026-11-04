#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <string>

#include <chordcore/Document.h>
#include <chordcore/Progression.h>
#include <chordcore/Scheduler.h>

/// Thin JUCE adapter around chordcore. No music logic lives here.
///
/// Scope: the editable project (chordcore::Document with undo/redo), MIDI-file export (milestone 1),
/// transport-synced MIDI output (milestone 3) and saving/restoring the project with the host session
/// (milestone 4).
///
/// Threading
///  - Message thread (editor) and host state calls (getState/setState, any non-audio thread) touch the
///    Document only under `documentLock`.
///  - The audio thread never touches the Document. It reads the scheduler under `schedulerLock` using a
///    try-lock: if an edit is swapping the note list at that instant the block simply emits nothing and
///    the scheduler resynchronises on the next block. The audio thread never blocks and never allocates.
///  - Every change rebuilds the scheduler's note list while still holding `documentLock`, so note lists
///    are installed in exactly the order the edits happened.
class ChordSketchProcessor final : public juce::AudioProcessor {
public:
    struct HistoryInfo {
        bool canUndo = false;
        bool canRedo = false;
        std::string undoLabel;
        std::string redoLabel;
    };

    ChordSketchProcessor();
    ~ChordSketchProcessor() override = default;

    // ---- project access (any non-audio thread)
    /// A copy of the current progression.
    chordcore::Progression getProgression() const;
    HistoryInfo getHistoryInfo() const;

    /// Applies an edit through chordcore::Document (validated; a rejected edit changes nothing).
    chordcore::Document::Result editProgression(const std::string& label,
                                                const std::function<void(chordcore::Progression&)>& mutate,
                                                const std::string& coalesceKey = {});
    bool undo();
    bool redo();

    /// Empty unless the last attempt to restore a saved state failed; then the reason (the current
    /// project is left untouched in that case).
    std::string getLastLoadError() const;

    /// Notified (asynchronously, on the message thread) after every change of the project or load error.
    juce::ChangeBroadcaster& changes() noexcept { return broadcaster; }

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

    /// The saved state is chordcore's versioned JSON document (UTF-8, no wrapper). Undo history is not saved.
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

private:
    /// Regenerates the playable notes from the document. Caller holds documentLock.
    void rebuildPlaybackLocked();

    mutable juce::CriticalSection documentLock;
    chordcore::Document document;  // guarded by documentLock
    std::string lastLoadError;     // guarded by documentLock

    juce::SpinLock schedulerLock;
    chordcore::PlaybackScheduler scheduler;  // guarded by schedulerLock

    std::atomic<double> fallbackTempo{120.0};  // read by the audio thread, written under documentLock
    double currentSampleRate = 44100.0;        // written in prepareToPlay, read by the audio thread

    juce::ChangeBroadcaster broadcaster;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordSketchProcessor)
};
