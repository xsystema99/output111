#include <chordcore/Scheduler.h>

#include <algorithm>
#include <cmath>

namespace chordcore {

void PlaybackScheduler::setNotes(std::vector<NoteEvent> notes) {
    notes_ = std::move(notes);
    active_.assign(notes_.size(), static_cast<char>(0));
    // Worst case per note in one block: release (jump), start, and end.
    events_.clear();
    events_.reserve(notes_.size() * 3 + 16);
    wasPlaying_ = false;
    expectedStartTick_ = 0.0;
    previousHi_ = 0.0;
}

void PlaybackScheduler::reset() {
    std::fill(active_.begin(), active_.end(), static_cast<char>(0));
    events_.clear();
    wasPlaying_ = false;
    expectedStartTick_ = 0.0;
}

std::size_t PlaybackScheduler::activeNoteCount() const {
    return static_cast<std::size_t>(std::count(active_.begin(), active_.end(), static_cast<char>(1)));
}

const std::vector<ScheduledEvent>& PlaybackScheduler::process(const TransportBlock& tb) {
    events_.clear();

    const bool valid = tb.numSamples > 0 && std::isfinite(tb.sampleRate) && tb.sampleRate > 0.0 &&
                       std::isfinite(tb.bpm) && tb.bpm > 0.0 && std::isfinite(tb.ppqStart);
    if (!valid) return events_;

    auto emit = [this](int offset, bool on, const NoteEvent& n) {
        events_.push_back(ScheduledEvent{offset, on, n.pitch, on ? n.velocity : 0, n.channel});
    };
    auto releaseAll = [this, &emit]() {
        for (std::size_t i = 0; i < notes_.size(); ++i) {
            if (active_[i]) {
                emit(0, false, notes_[i]);
                active_[i] = 0;
            }
        }
    };
    auto finish = [this]() -> const std::vector<ScheduledEvent>& {
        std::sort(events_.begin(), events_.end(), [](const ScheduledEvent& a, const ScheduledEvent& b) {
            if (a.sampleOffset != b.sampleOffset) return a.sampleOffset < b.sampleOffset;
            if (a.noteOn != b.noteOn) return !a.noteOn;  // note-offs first
            if (a.pitch != b.pitch) return a.pitch < b.pitch;
            return a.channel < b.channel;
        });
        return events_;
    };

    if (!tb.playing) {
        releaseAll();
        wasPlaying_ = false;
        return finish();
    }

    const double ticksPerSample = tb.bpm / 60.0 * kTicksPerQuarter / tb.sampleRate;
    const double hostT0 = tb.ppqStart * kTicksPerQuarter;

    // Continuation check. The tolerance absorbs floating-point drift and hosts that round ppq slightly;
    // a real locate or loop wrap is far larger than a couple of ticks.
    const double tolerance = 2.0 + 0.05 * tb.numSamples * ticksPerSample;
    const bool fresh = !wasPlaying_ || std::fabs(hostT0 - expectedStartTick_) > tolerance;

    // Sample i "owns" the ticks within half a sample either side of its position, so this block owns
    // the tick window [lo, hi). Continuing blocks chain exactly (this lo == previous hi, bit for bit),
    // so every tick belongs to exactly one block even across tempo changes and floating-point noise.
    // Continuing blocks also use our own running position, not the host's, to stay seamless.
    const double t0 = fresh ? hostT0 : expectedStartTick_;
    const double lo = fresh ? t0 - 0.5 * ticksPerSample : previousHi_;
    const double hi = t0 + (tb.numSamples - 0.5) * ticksPerSample;

    if (fresh) releaseAll();

    auto offsetOf = [&](double tick) {
        const long long o = static_cast<long long>(std::floor((tick - t0) / ticksPerSample + 0.5));
        return static_cast<int>(std::clamp<long long>(o, 0, tb.numSamples - 1));
    };

    for (std::size_t i = 0; i < notes_.size(); ++i) {
        const NoteEvent& n = notes_[i];
        const double s = static_cast<double>(n.startTick);
        const double e = static_cast<double>(n.startTick + n.lengthTicks);

        if (active_[i]) {
            if (e < hi) {
                emit(offsetOf(e), false, n);
                active_[i] = 0;
            }
            continue;
        }

        const bool startsInBlock = s >= lo && s < hi;
        const bool soundingAtStart = fresh && s < lo && e > lo;
        if (!startsInBlock && !soundingAtStart) continue;

        const int onOffset = startsInBlock ? offsetOf(s) : 0;
        emit(onOffset, true, n);
        if (e < hi) {
            // Shorter than the rest of the block: it ends here. The off must land after its own on, or
            // the sort would put it first and leave a stuck note.
            int offOffset = offsetOf(e);
            if (offOffset <= onOffset) offOffset = onOffset + 1;
            if (offOffset <= tb.numSamples - 1) {
                emit(offOffset, false, n);
            } else {
                active_[i] = 1;  // no room left in this block: released at offset 0 of the next one
            }
        } else {
            active_[i] = 1;
        }
    }

    wasPlaying_ = true;
    expectedStartTick_ = t0 + tb.numSamples * ticksPerSample;
    previousHi_ = hi;
    return finish();
}

}  // namespace chordcore
