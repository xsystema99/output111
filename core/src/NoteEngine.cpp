#include <chordcore/NoteEngine.h>

#include <algorithm>
#include <stdexcept>

namespace chordcore {

std::vector<NoteEvent> generateNotes(const Progression& progression, const GenerationOptions& options) {
    const auto errors = progression.validate();
    if (!errors.empty()) throw std::invalid_argument("invalid progression: " + errors.front());
    if (options.velocity < 1 || options.velocity > 127) throw std::invalid_argument("velocity must be 1..127");
    if (options.channel < 0 || options.channel > 15) throw std::invalid_argument("channel must be 0..15");
    if (options.releaseGapTicks < 0) throw std::invalid_argument("releaseGapTicks must be >= 0");

    std::vector<NoteEvent> notes;
    for (const auto& block : progression.blocks) {
        const std::vector<int> pitches = voiceChord(progression.key, block.chord, options.voicing);
        const Tick length = std::max<Tick>(1, block.durationTicks - options.releaseGapTicks);
        for (int pitch : pitches) {
            notes.push_back(NoteEvent{pitch, block.startTick, length, options.velocity, options.channel});
        }
    }

    std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.pitch < b.pitch;
    });
    return notes;
}

}  // namespace chordcore
