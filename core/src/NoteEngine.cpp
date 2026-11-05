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
        VoicingOptions voicing = options.voicing;
        voicing.rootBase += 12 * (progression.octave + block.octave);  // project octave + this chord's octave
        const std::vector<int> pitches = voiceChord(progression.key, block.chord, voicing);
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

std::vector<NoteEvent> generateMelodyNotes(const Progression& progression) {
    const auto errors = progression.validate();
    if (!errors.empty()) throw std::invalid_argument("invalid progression: " + errors.front());
    const int channel = progression.melodySettings.melodyChannel - 1;
    std::vector<NoteEvent> notes;
    notes.reserve(progression.melody.size());
    for (const auto& n : progression.melody) {
        notes.push_back(NoteEvent{n.pitch, n.startTick, n.durationTicks, n.velocity, channel});
    }
    std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        return a.pitch < b.pitch;
    });
    return notes;
}

std::vector<NoteEvent> generateProjectNotes(const Progression& progression, GenerationOptions options) {
    options.channel = progression.melodySettings.chordChannel - 1;
    std::vector<NoteEvent> notes;
    if (!progression.melodySettings.muteChords) notes = generateNotes(progression, options);
    if (!progression.melodySettings.muteMelody) {
        auto melody = generateMelodyNotes(progression);
        notes.insert(notes.end(), melody.begin(), melody.end());
    }
    std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        if (a.startTick != b.startTick) return a.startTick < b.startTick;
        if (a.pitch != b.pitch) return a.pitch < b.pitch;
        return a.channel < b.channel;
    });
    return notes;
}

}  // namespace chordcore
