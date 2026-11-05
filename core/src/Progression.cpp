#include <chordcore/Progression.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chordcore {

Tick Progression::endTick() const {
    Tick end = 0;
    for (const auto& block : blocks) end = std::max(end, block.endTick());
    return end;
}

Tick Progression::lengthTicks() const {
    const Tick end = endTick();
    const Tick bar = timeSignature.ticksPerBar();
    const Tick content = end == 0 ? 0 : ((end + bar - 1) / bar) * bar;
    const Tick requested = lengthBars > 0 ? static_cast<Tick>(lengthBars) * bar : 0;
    return std::max(content, requested);
}

std::vector<std::string> Progression::validate() const {
    std::vector<std::string> errors;

    if (!key.isValid()) errors.push_back("key tonic must be a pitch class 0..11");
    if (!(std::isfinite(tempoBpm) && tempoBpm >= 20.0 && tempoBpm <= 999.0)) {
        errors.push_back("tempo must be between 20 and 999 BPM");
    }
    if (!timeSignature.isValid()) errors.push_back("invalid time signature");
    if (octave < -kMaxOctaveShift || octave > kMaxOctaveShift) errors.push_back("octave must be between -2 and +2");
    if (lengthBars < 0 || lengthBars > kMaxLengthBars) errors.push_back("length must be 0 (automatic) to 256 bars");

    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const auto& block = blocks[i];
        const std::string where = "block " + std::to_string(i) + ": ";
        if (!block.chord.isValid()) errors.push_back(where + "invalid chord");
        if (block.startTick < 0) errors.push_back(where + "start must be >= 0");
        if (block.durationTicks < 1) errors.push_back(where + "duration must be >= 1 tick");
        if (block.octave < -kMaxOctaveShift || block.octave > kMaxOctaveShift) errors.push_back(where + "octave must be between -2 and +2");

        // Every voiced note must fit MIDI 0..127 (a 13th chord at the highest octave settings can pass 127).
        if (key.isValid() && block.chord.isValid() && block.octave >= -kMaxOctaveShift && block.octave <= kMaxOctaveShift &&
            octave >= -kMaxOctaveShift && octave <= kMaxOctaveShift) {
            VoicingOptions voicing;
            voicing.rootBase += 12 * (octave + block.octave);  // as in generateNotes
            try {
                (void)voiceChord(key, block.chord, voicing);
            } catch (const std::out_of_range&) {
                errors.push_back(where + "this chord would go outside the MIDI note range at this octave; lower the octave or simplify the chord");
            }
        }
    }

    // Overlap check on a start-sorted copy of the indices.
    std::vector<std::size_t> order(blocks.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [this](std::size_t a, std::size_t b) {
        return blocks[a].startTick < blocks[b].startTick;
    });
    for (std::size_t i = 1; i < order.size(); ++i) {
        const auto& previous = blocks[order[i - 1]];
        const auto& current = blocks[order[i]];
        if (current.startTick < previous.endTick()) {
            errors.push_back("blocks " + std::to_string(order[i - 1]) + " and " + std::to_string(order[i]) + " overlap");
        }
    }

    return errors;
}

Progression makeDemoProgression() {
    Progression p;
    p.key = Key{0, Mode::Major};
    p.tempoBpm = 120.0;
    p.timeSignature = TimeSignature{4, 4};

    const Tick bar = p.timeSignature.ticksPerBar();
    // I, V, vi, IV
    const ChordSpec chords[4] = {
        {1, 0, Quality::Major, Seventh::None, 0},
        {5, 0, Quality::Major, Seventh::None, 0},
        {6, 0, Quality::Minor, Seventh::None, 0},
        {4, 0, Quality::Major, Seventh::None, 0},
    };
    for (int i = 0; i < 4; ++i) p.blocks.push_back(ChordBlock{chords[i], bar * i, bar});
    return p;
}

}  // namespace chordcore
