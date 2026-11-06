#include <chordcore/MidiFile.h>
#include <chordcore/MidiFilename.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chordcore {

namespace {

using Bytes = std::vector<std::uint8_t>;

void put8(Bytes& out, unsigned value) { out.push_back(static_cast<std::uint8_t>(value & 0xFFu)); }

void put16(Bytes& out, unsigned value) {
    put8(out, value >> 8);
    put8(out, value);
}

void put32(Bytes& out, std::uint32_t value) {
    put8(out, value >> 24);
    put8(out, value >> 16);
    put8(out, value >> 8);
    put8(out, value);
}

void putTag(Bytes& out, const char* tag) {
    for (int i = 0; i < 4; ++i) put8(out, static_cast<unsigned char>(tag[i]));
}

// Ordering of events that share a tick.
enum Order { kMeta = 0, kNoteOff = 1, kNoteOn = 2, kEndOfTrack = 3 };

struct RawEvent {
    Tick tick;
    int order;
    int pitch;  // tie-break for determinism
    Bytes bytes;
};

Bytes metaEvent(std::uint8_t type, const Bytes& payload) {
    Bytes e{0xFF, type};
    appendVariableLengthQuantity(e, static_cast<std::uint32_t>(payload.size()));
    e.insert(e.end(), payload.begin(), payload.end());
    return e;
}

void sortEvents(std::vector<RawEvent>& events) {
    std::stable_sort(events.begin(), events.end(), [](const RawEvent& a, const RawEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        if (a.order != b.order) return a.order < b.order;
        return a.pitch < b.pitch;
    });
}

void appendTrack(Bytes& file, std::vector<RawEvent>& events) {
    sortEvents(events);
    Bytes track;
    Tick previousTick = 0;
    for (const auto& event : events) {
        const Tick delta = event.tick - previousTick;
        if (delta < 0 || delta > 0x0FFFFFFF) throw std::out_of_range("delta time not representable");
        appendVariableLengthQuantity(track, static_cast<std::uint32_t>(delta));
        track.insert(track.end(), event.bytes.begin(), event.bytes.end());
        previousTick = event.tick;
    }
    putTag(file, "MTrk");
    put32(file, static_cast<std::uint32_t>(track.size()));
    file.insert(file.end(), track.begin(), track.end());
}

int log2OfPowerOfTwo(int v) {
    int n = 0;
    while (v > 1) {
        v >>= 1;
        ++n;
    }
    return n;
}

}  // namespace

void appendVariableLengthQuantity(std::vector<std::uint8_t>& out, std::uint32_t value) {
    if (value > 0x0FFFFFFFu) throw std::out_of_range("variable-length quantity exceeds 0x0FFFFFFF");
    std::uint8_t groups[4];
    int count = 0;
    groups[count++] = static_cast<std::uint8_t>(value & 0x7Fu);
    while ((value >>= 7) != 0) groups[count++] = static_cast<std::uint8_t>((value & 0x7Fu) | 0x80u);
    while (count > 0) out.push_back(groups[--count]);
}

int midiKeySignatureSharps(const Key& key) {
    if (!key.isValid()) throw std::invalid_argument("invalid key");
    const int majorTonic = key.mode == Mode::Major ? key.tonic : (key.tonic + 3) % 12;
    int sharps = (majorTonic * 7) % 12;  // position on the circle of fifths, 0..11
    if (sharps > 6) sharps -= 12;
    return sharps;
}

namespace {

/// Track name, time signature, tempo and (optionally) key signature at tick 0.
void addHeaderEvents(std::vector<RawEvent>& events, const Progression& progression, const std::string& trackName,
                     bool timing, bool writeKeySignature, const std::vector<std::string>& textMetadata = {}) {
    if (!trackName.empty()) events.push_back({0, kMeta, 0, metaEvent(0x03, Bytes(trackName.begin(), trackName.end()))});
    if (!timing) return;

    const auto& ts = progression.timeSignature;
    events.push_back({0, kMeta, 0,
                      metaEvent(0x58, {static_cast<std::uint8_t>(ts.numerator),
                                       static_cast<std::uint8_t>(log2OfPowerOfTwo(ts.denominator)), 24, 8})});

    const long long microsPerQuarter = std::llround(60000000.0 / progression.tempoBpm);
    if (microsPerQuarter < 1 || microsPerQuarter > 0xFFFFFF) throw std::invalid_argument("tempo not representable in SMF");
    events.push_back({0, kMeta, 0,
                      metaEvent(0x51, {static_cast<std::uint8_t>((microsPerQuarter >> 16) & 0xFF),
                                       static_cast<std::uint8_t>((microsPerQuarter >> 8) & 0xFF),
                                       static_cast<std::uint8_t>(microsPerQuarter & 0xFF)})});

    if (writeKeySignature) {
        const int sharps = midiKeySignatureSharps(progression.key);
        events.push_back({0, kMeta, 0,
                          metaEvent(0x59, {static_cast<std::uint8_t>(static_cast<std::int8_t>(sharps)),
                                           static_cast<std::uint8_t>(progression.key.mode == Mode::Minor ? 1 : 0)})});
    }
    for (const auto& text : textMetadata) events.push_back({0, kMeta, 0, metaEvent(0x01, Bytes(text.begin(), text.end()))});
}

void addNoteEvents(std::vector<RawEvent>& events, const std::vector<NoteEvent>& notes, Tick& lastEnd) {
    for (const auto& note : notes) {
        if (note.pitch < 0 || note.pitch > 127) throw std::invalid_argument("note pitch must be 0..127");
        if (note.velocity < 1 || note.velocity > 127) throw std::invalid_argument("note velocity must be 1..127");
        if (note.channel < 0 || note.channel > 15) throw std::invalid_argument("note channel must be 0..15");
        if (note.startTick < 0) throw std::invalid_argument("note start must be >= 0");
        if (note.lengthTicks < 1) throw std::invalid_argument("note length must be >= 1 tick");

        const Tick end = note.startTick + note.lengthTicks;
        lastEnd = std::max(lastEnd, end);

        events.push_back({note.startTick, kNoteOn, note.pitch,
                          {static_cast<std::uint8_t>(0x90 | note.channel), static_cast<std::uint8_t>(note.pitch),
                           static_cast<std::uint8_t>(note.velocity)}});
        events.push_back({end, kNoteOff, note.pitch,
                          {static_cast<std::uint8_t>(0x80 | note.channel), static_cast<std::uint8_t>(note.pitch), 0}});
    }
}

void checkProgression(const Progression& progression) {
    const auto errors = progression.validate();
    if (!errors.empty()) throw std::invalid_argument("invalid progression: " + errors.front());
}

}  // namespace

std::vector<std::uint8_t> buildMidiFile(const Progression& progression, const std::vector<NoteEvent>& notes,
                                        const MidiExportOptions& options) {
    checkProgression(progression);
    std::vector<RawEvent> events;
    addHeaderEvents(events, progression, options.trackName, true, options.writeKeySignature, options.textMetadata);
    Tick lastEnd = 0;
    addNoteEvents(events, notes, lastEnd);
    events.push_back({std::max(progression.lengthTicks(), lastEnd), kEndOfTrack, 0, {0xFF, 0x2F, 0x00}});

    Bytes file;
    putTag(file, "MThd");
    put32(file, 6);
    put16(file, 0);  // format 0
    put16(file, 1);  // one track
    put16(file, static_cast<unsigned>(kTicksPerQuarter));
    appendTrack(file, events);
    return file;
}

std::vector<std::uint8_t> exportProgressionToMidi(const Progression& progression, const GenerationOptions& generation,
                                                  const MidiExportOptions& options) {
    return buildMidiFile(progression, generateNotes(progression, generation), options);
}

namespace {

std::vector<NoteEvent> clipToBars(const std::vector<NoteEvent>& notes, Tick from, Tick to) {
    std::vector<NoteEvent> clipped;
    for (NoteEvent n : notes) {
        const Tick end = n.startTick + n.lengthTicks;
        if (end <= from || n.startTick >= to) continue;
        const Tick start = std::max(n.startTick, from);
        n.lengthTicks = std::min(end, to) - start;
        n.startTick = start - from;
        clipped.push_back(n);
    }
    return clipped;
}

}  // namespace

std::vector<std::uint8_t> exportProgressionRangeToMidi(const Progression& progression, int firstBar, int lastBar,
                                                       const GenerationOptions& generation,
                                                       const MidiExportOptions& options) {
    if (firstBar < 1 || lastBar < firstBar || lastBar > kMaxLengthBars) {
        throw std::invalid_argument("bar range must satisfy 1 <= first <= last <= 256");
    }
    const Tick bar = progression.timeSignature.ticksPerBar();
    const Tick from = static_cast<Tick>(firstBar - 1) * bar;
    const Tick to = static_cast<Tick>(lastBar) * bar;

    const auto clipped = clipToBars(generateNotes(progression, generation), from, to);  // the one note generator

    Progression header = progression;  // tempo, meter and key only; its length defines the end-of-track
    header.blocks.clear();
    header.melody.clear();
    header.lengthBars = lastBar - firstBar + 1;
    return buildMidiFile(header, clipped, options);
}

const char* exportFileName(ExportPart part) {
    switch (part) {
        case ExportPart::Chords: return "Chords.mid";
        case ExportPart::Melody: return "Melody.mid";
        case ExportPart::Combined: return "Combined.mid";
    }
    return "Combined.mid";
}

std::string exportFileName(ExportPart part, bool originalVoicing) {
    return std::string(originalVoicing && part != ExportPart::Melody ? "Original " : "") + exportFileName(part);
}

std::vector<std::uint8_t> exportProject(const Progression& progression, const ProjectExportOptions& options,
                                        const GenerationOptions& generation) {
    checkProgression(progression);
    const bool ranged = options.firstBar != 0 || options.lastBar != 0;
    if (ranged && (options.firstBar < 1 || options.lastBar < options.firstBar || options.lastBar > kMaxLengthBars)) {
        throw std::invalid_argument("bar range must satisfy 1 <= first <= last <= 256");
    }

    // The same events playback uses; the chord channel comes from the project settings.
    GenerationOptions chordOptions = generation;
    chordOptions.channel = progression.melodySettings.chordChannel - 1;
    chordOptions.originalVoicing = chordOptions.originalVoicing || options.originalVoicing;
    std::vector<NoteEvent> chords = generateNotes(progression, chordOptions);
    std::vector<NoteEvent> melody = generateMelodyNotes(progression);
    if (options.melodyOnChannel1) {
        for (auto& n : melody) n.channel = 0;
    }

    Progression header = progression;
    if (ranged) {
        const Tick bar = progression.timeSignature.ticksPerBar();
        const Tick from = static_cast<Tick>(options.firstBar - 1) * bar;
        const Tick to = static_cast<Tick>(options.lastBar) * bar;
        chords = clipToBars(chords, from, to);
        melody = clipToBars(melody, from, to);
        header.blocks.clear();
        header.melody.clear();
        header.lengthBars = options.lastBar - options.firstBar + 1;
    }
    const Tick endTick = header.lengthTicks();
    // Both harmonic representations travel with every file, whatever the filename format (from the unclipped project).
    // The melody-only file stays independent of the chords (its own naming and contents), so it carries none.
    const std::vector<std::string> metadata =
        options.part == ExportPart::Melody ? std::vector<std::string>{} : midiMetadataText(progression, options.firstBar, options.lastBar);

    if (options.part == ExportPart::Chords || options.part == ExportPart::Melody) {
        MidiExportOptions single;
        single.trackName = options.part == ExportPart::Chords ? "Chords" : "Melody";
        single.writeKeySignature = options.writeKeySignature;
        single.textMetadata = metadata;
        return buildMidiFile(header, options.part == ExportPart::Chords ? chords : melody, single);
    }

    // Combined: Format 1. Track 0 carries name, meter, tempo and key for every track; then Chords and Melody.
    std::vector<RawEvent> conductor;
    addHeaderEvents(conductor, header, "ChordSketch", true, options.writeKeySignature, metadata);
    conductor.push_back({endTick, kEndOfTrack, 0, {0xFF, 0x2F, 0x00}});

    auto noteTrack = [&](const char* name, const std::vector<NoteEvent>& notes) {
        std::vector<RawEvent> events;
        addHeaderEvents(events, header, name, false, false);
        Tick lastEnd = 0;
        addNoteEvents(events, notes, lastEnd);
        events.push_back({std::max(endTick, lastEnd), kEndOfTrack, 0, {0xFF, 0x2F, 0x00}});
        return events;
    };
    auto chordTrack = noteTrack("Chords", chords);
    auto melodyTrack = noteTrack("Melody", melody);

    Bytes file;
    putTag(file, "MThd");
    put32(file, 6);
    put16(file, 1);  // format 1
    put16(file, 3);  // conductor + chords + melody
    put16(file, static_cast<unsigned>(kTicksPerQuarter));
    appendTrack(file, conductor);
    appendTrack(file, chordTrack);
    appendTrack(file, melodyTrack);
    return file;
}

}  // namespace chordcore
