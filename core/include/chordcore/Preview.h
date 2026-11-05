#pragma once

#include <chordcore/Scheduler.h>

namespace chordcore {

/// An internal transport for "Preview": plays the progression from the start, at the project tempo, while the
/// host transport is stopped. It does not generate notes itself: it produces the TransportBlock that is fed to
/// the same PlaybackScheduler as the host transport, so a preview is sample-accurate, releases its notes the
/// same way, and plays exactly what the exported file contains.
///
/// Rules
///  - A preview starts at position 0 when requested while the host is stopped.
///  - While the host is playing, the host wins: a running preview is cancelled and a request is ignored.
///  - The preview ends by itself once its position reaches `endPpq` (the end of the last chord): one final
///    "stopped" block is produced so the scheduler releases any sounding note, then it reports the request done.
///  - Withdrawing the request stops it the same way (one stopped block, then nothing).
///
/// Audio-thread object: no allocation, not thread-safe (the plugin guards it with the scheduler's lock).
class PreviewTransport {
public:
    struct Result {
        bool overrides = false;        ///< true: feed `block` to the scheduler instead of the host's block
        TransportBlock block;          ///< valid when overrides
        bool requestStillWanted = false;  ///< false: the caller should clear its "preview requested" flag
    };

    /// Advances by one audio block.
    ///   requested    the user wants the preview running
    ///   hostPlaying  the host transport is running (it takes precedence)
    ///   bpm          tempo to preview at (the project tempo)
    ///   endPpq       preview length in quarter notes
    Result step(bool requested, bool hostPlaying, double bpm, double sampleRate, int numSamples, double endPpq);

    bool running() const noexcept { return running_; }
    void reset() noexcept;

private:
    bool running_ = false;
    double ppq_ = 0.0;
};

}  // namespace chordcore
