#include <chordcore/Audition.h>

#include <algorithm>
#include <cmath>

namespace chordcore {

void AuditionPlayer::request(std::vector<int> pitches, int velocity, int channel) {
    pendingPitches = std::move(pitches);
    pendingVelocity = std::clamp(velocity, 1, 127);
    pendingChannel = std::clamp(channel, 0, 15);
    havePending = !pendingPitches.empty();
    pendingStop = false;
}

void AuditionPlayer::stopAll() {
    havePending = false;
    pendingStop = true;
}

void AuditionPlayer::reset() {
    havePending = false;
    pendingStop = false;
    active.clear();
    remainingSamples = 0.0;
}

const std::vector<ScheduledEvent>& AuditionPlayer::process(int numSamples, double sampleRate) {
    events.clear();
    const auto releaseAll = [&](int offset) {
        for (const auto& v : active) events.push_back({offset, false, v.pitch, 0, v.channel});
        active.clear();
        remainingSamples = 0.0;
    };

    if (pendingStop) {
        releaseAll(0);
        pendingStop = false;
    }
    if (havePending) {
        releaseAll(0);  // a new chord replaces the old one
        auto& pitches = pendingPitches;  // sorted in place: it is cleared below, so no copy (and no allocation)
        std::sort(pitches.begin(), pitches.end());
        pitches.erase(std::unique(pitches.begin(), pitches.end()), pitches.end());
        for (int p : pitches) {
            if (active.size() >= kMaxVoices) break;
            if (p < 0 || p > 127) continue;
            events.push_back({0, true, p, pendingVelocity, pendingChannel});
            active.push_back({p, pendingChannel});
        }
        remainingSamples = std::max(1.0, hold * sampleRate);
        havePending = false;
        pendingPitches.clear();
    }

    if (!active.empty() && numSamples > 0) {
        // Count the block down; the release lands on the exact sample (at least one sample after the note-on).
        const bool startedThisBlock = !events.empty() && events.back().noteOn;
        const double available = static_cast<double>(numSamples);
        if (remainingSamples < available) {
            int offset = static_cast<int>(std::floor(remainingSamples));
            if (startedThisBlock) offset = std::max(offset, 1);
            offset = std::clamp(offset, 0, numSamples - 1);
            releaseAll(offset);
        } else {
            remainingSamples -= available;
        }
    }
    return events;
}

}  // namespace chordcore
