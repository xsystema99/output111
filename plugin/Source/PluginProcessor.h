#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <string>

#include <chordcore/Document.h>
#include <chordcore/Audition.h>
#include <chordcore/Preview.h>
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

    /// Where the host transport is, as last seen by the audio thread (for the editor's playhead line).
    struct Playhead {
        bool valid = false;    ///< the host reports a musical position
        bool playing = false;
        double ppq = 0.0;      ///< quarter notes from the host's origin
    };

    ChordSketchProcessor();
    ~ChordSketchProcessor() override = default;

    Playhead getPlayhead() const noexcept;

    /// "Preview": plays the progression from bar 1 at the project tempo while the host transport is stopped
    /// (it needs the host to keep calling processBlock). Cancelled automatically when the host starts playing
    /// and when the progression ends. Any thread.
    void setPreviewRequested(bool requested) noexcept { previewRequested.store(requested); }
    bool isPreviewRequested() const noexcept { return previewRequested.load(); }
    bool isPreviewRunning() const noexcept { return previewRunning.load(); }

    /// Plays `chord` once through the MIDI output (about 0.8 s), e.g. when a palette chord is clicked. Voiced by the
    /// same note engine as playback and export, at the project octave plus `blockOctave`. Ignored while the host
    /// plays or Preview runs. Message thread.
    void auditionChord(const chordcore::ChordSpec& chord, int blockOctave);

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
    /// firstBar/lastBar 0 = the whole project; otherwise bars firstBar..lastBar (1-based, inclusive) as their own
    /// file (chordcore::exportProgressionRangeToMidi). Throws std::invalid_argument for a bad range.
    std::vector<std::uint8_t> exportMidiBytes(int firstBar = 0, int lastBar = 0) const;

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

    chordcore::PreviewTransport preview;  // guarded by schedulerLock (audio thread)
    std::atomic<bool> previewRequested{false};
    std::atomic<bool> previewRunning{false};
    std::atomic<double> previewEndPpq{0.0};  // end of the last chord in quarter notes, written under documentLock

    juce::SpinLock auditionLock;
    chordcore::AuditionPlayer audition;  // guarded by auditionLock

    std::atomic<bool> playheadValid{false};
    std::atomic<bool> playheadPlaying{false};
    std::atomic<double> playheadPpq{0.0};

    juce::ChangeBroadcaster broadcaster;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordSketchProcessor)
};
