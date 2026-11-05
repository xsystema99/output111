#include <chordcore/Preview.h>

#include <cmath>

namespace chordcore {

void PreviewTransport::reset() noexcept {
    running_ = false;
    ppq_ = 0.0;
}

PreviewTransport::Result PreviewTransport::step(bool requested, bool hostPlaying, double bpm, double sampleRate,
                                                int numSamples, double endPpq) {
    Result result;

    auto stoppedBlock = [&] {
        TransportBlock block;
        block.playing = false;
        block.ppqStart = ppq_;
        block.bpm = bpm;
        block.sampleRate = sampleRate;
        block.numSamples = numSamples;
        return block;
    };

    if (hostPlaying) {
        // The host drives the scheduler; its own "playing" block takes care of the notes.
        running_ = false;
        return result;  // overrides = false, requestStillWanted = false
    }

    if (!requested) {
        if (running_) {
            running_ = false;
            result.overrides = true;
            result.block = stoppedBlock();  // lets the scheduler release what is sounding
        }
        return result;
    }

    const bool valid = numSamples > 0 && std::isfinite(sampleRate) && sampleRate > 0.0 && std::isfinite(bpm) && bpm > 0.0 &&
                       std::isfinite(endPpq) && endPpq > 0.0;
    if (!valid) {
        // Nothing sensible to play (no chords, or a bad block): finish the request.
        if (running_) {
            running_ = false;
            result.overrides = true;
            result.block = stoppedBlock();
        }
        return result;
    }

    if (!running_) {
        running_ = true;
        ppq_ = 0.0;
    }

    if (ppq_ >= endPpq) {
        running_ = false;
        result.overrides = true;
        result.block = stoppedBlock();
        return result;  // requestStillWanted = false: finished
    }

    result.overrides = true;
    result.requestStillWanted = true;
    result.block.playing = true;
    result.block.ppqStart = ppq_;
    result.block.bpm = bpm;
    result.block.sampleRate = sampleRate;
    result.block.numSamples = numSamples;
    ppq_ += static_cast<double>(numSamples) * bpm / (60.0 * sampleRate);
    return result;
}

}  // namespace chordcore
