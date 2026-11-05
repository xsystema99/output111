#include <chordcore/NoteEngine.h>

#include <algorithm>
#include <stdexcept>

namespace chordcore {

std::vector<VoicedChord> voiceProgression(const Progression& progression, const VoicingOptions& voicing, bool originalVoicing) {
    const auto errors = progression.validate();
    if (!errors.empty()) throw std::invalid_argument("invalid progression: " + errors.front());
    std::vector<std::size_t> order(progression.blocks.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return progression.blocks[a].startTick < progression.blocks[b].startTick;
    });
    std::vector<VoicedChord> result(progression.blocks.size());
    std::vector<int> previous;
    for (std::size_t i : order) {
        const auto& block = progression.blocks[i];
        VoicingOptions v = voicing;
        const int shift = progression.octave + block.octave;  // project octave + this chord's octave
        v.rootBase += 12 * shift;
        VoicingSettings settings = progression.voicingFor(i);
        if (originalVoicing) settings.density = 0;
        result[i] = voiceChordWithSettings(progression.key, block.chord, v, settings, shift, previous.empty() ? nullptr : &previous);
        previous = result[i].pitches();
    }
    return result;
}

std::vector<NoteEvent> generateNotes(const Progression& progression, const GenerationOptions& options) {
    const auto errors = progression.validate();
    if (!errors.empty()) throw std::invalid_argument("invalid progression: " + errors.front());
    if (options.velocity < 1 || options.velocity > 127) throw std::invalid_argument("velocity must be 1..127");
    if (options.channel < 0 || options.channel > 15) throw std::invalid_argument("channel must be 0..15");
    if (options.releaseGapTicks < 0) throw std::invalid_argument("releaseGapTicks must be >= 0");

    std::vector<NoteEvent> notes;
    std::vector<EventOrigin> origins;
    const std::vector<VoicedChord> voiced = voiceProgression(progression, options.voicing, options.originalVoicing);
    for (std::size_t i = 0; i < progression.blocks.size(); ++i) {
        const auto& block = progression.blocks[i];
        const Tick length = std::max<Tick>(1, block.durationTicks - options.releaseGapTicks);
        for (const auto& n : voiced[i].notes) {
            notes.push_back(NoteEvent{n.pitch, block.startTick, length, options.velocity, options.channel});
            origins.push_back(EventOrigin{i, n.voiceId, n.role});
        }
    }
    if (options.performance != nullptr) options.performance->apply(progression, voiced, notes, origins);

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
