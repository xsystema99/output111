#pragma once

#include <vector>

#include <chordcore/Scheduler.h>

namespace chordcore {

/// A short one-shot chord for auditioning (clicking a palette chord). It produces the same ScheduledEvent
/// currency as the playback scheduler and is JUCE-free, so it is unit-tested.
///
/// Rules (the "no stuck notes" guarantees):
///  - request() replaces whatever is sounding: the old notes are released at the start of the next block, then
///    the new ones start in that same block (offset 0), each pitch once.
///  - Every note-on is followed by exactly one note-off, `holdSeconds` later (to the sample), also across blocks.
///  - stopAll() releases everything that sounds at the start of the next block and drops any pending request.
///  - reset() forgets everything without emitting events (use only when the host has already flushed).
/// Not thread-safe: the owner serialises calls (the plugin does it with a lock).
class AuditionPlayer {
public:
    explicit AuditionPlayer(double holdSeconds = 0.8) : hold(holdSeconds) {}

    void request(std::vector<int> pitches, int velocity, int channel);
    void stopAll();
    void reset();

    /// Events for the next block of `numSamples` at `sampleRate`.
    std::vector<ScheduledEvent> process(int numSamples, double sampleRate);

    bool sounding() const noexcept { return !active.empty(); }

private:
    struct Voice { int pitch; int channel; };

    double hold;
    bool havePending = false;
    bool pendingStop = false;
    std::vector<int> pendingPitches;
    int pendingVelocity = 96;
    int pendingChannel = 0;

    std::vector<Voice> active;
    double remainingSamples = 0.0;
};

}  // namespace chordcore
