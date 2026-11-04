// Standard MIDI File export. The bytes are checked three ways:
//   1. a hand-assembled golden file (byte-for-byte)
//   2. VLQ vectors taken from the SMF specification
//   3. a round trip through tests/support/SmfParser (independent of the writer), including a seeded
//      property test over random progressions

#include <catch2/catch_test_macros.hpp>

#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>

#include "support/SmfParser.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace chordcore;
using Bytes = std::vector<std::uint8_t>;

namespace {

const ChordSpec kI{1, 0, Quality::Major, Seventh::None, 0};
const ChordSpec kV{5, 0, Quality::Major, Seventh::None, 0};

Bytes vlq(std::uint32_t v) {
    Bytes b;
    appendVariableLengthQuantity(b, v);
    return b;
}

smf::File parsed(const Bytes& b) { return smf::parse(b); }

std::uint32_t tempoMicros(const smf::Track& t) {
    const auto ev = smf::metaEvents(t, 0x51);
    REQUIRE(ev.size() == 1);
    REQUIRE(ev[0]->data.size() == 3);
    return (std::uint32_t(ev[0]->data[0]) << 16) | (std::uint32_t(ev[0]->data[1]) << 8) | ev[0]->data[2];
}

Progression singleBlock(Tick start, Tick duration) {
    Progression p;
    p.blocks = {ChordBlock{kI, start, duration}};
    return p;
}

}  // namespace

// ------------------------------------------------------------------------------------------------
// Variable-length quantities (test vectors from the SMF 1.0 specification)
// ------------------------------------------------------------------------------------------------

TEST_CASE("appendVariableLengthQuantity: specification vectors", "[midi][vlq]") {
    CHECK(vlq(0x00000000) == Bytes{0x00});
    CHECK(vlq(0x00000040) == Bytes{0x40});
    CHECK(vlq(0x0000007F) == Bytes{0x7F});
    CHECK(vlq(0x00000080) == Bytes{0x81, 0x00});
    CHECK(vlq(0x00002000) == Bytes{0xC0, 0x00});
    CHECK(vlq(0x00003FFF) == Bytes{0xFF, 0x7F});
    CHECK(vlq(0x00004000) == Bytes{0x81, 0x80, 0x00});
    CHECK(vlq(0x00100000) == Bytes{0xC0, 0x80, 0x00});
    CHECK(vlq(0x001FFFFF) == Bytes{0xFF, 0xFF, 0x7F});
    CHECK(vlq(0x00200000) == Bytes{0x81, 0x80, 0x80, 0x00});
    CHECK(vlq(0x08000000) == Bytes{0xC0, 0x80, 0x80, 0x00});
    CHECK(vlq(0x0FFFFFFF) == Bytes{0xFF, 0xFF, 0xFF, 0x7F});
    CHECK(vlq(3840) == Bytes{0x9E, 0x00});  // one 4/4 bar at 960 PPQ
}

TEST_CASE("appendVariableLengthQuantity: appends and rejects out-of-range", "[midi][vlq]") {
    Bytes b{0xAA};
    appendVariableLengthQuantity(b, 0x80);
    CHECK(b == Bytes{0xAA, 0x81, 0x00});
    Bytes c;
    CHECK_THROWS_AS(appendVariableLengthQuantity(c, 0x10000000u), std::out_of_range);
}

// ------------------------------------------------------------------------------------------------
// Golden bytes
// ------------------------------------------------------------------------------------------------

TEST_CASE("buildMidiFile: golden bytes for one C major chord (4/4, 120 BPM, one bar)", "[midi][golden]") {
    Progression p;
    p.blocks = {ChordBlock{kI, 0, 3840}};

    // Hand-assembled from the SMF specification.
    const Bytes expected = {
        // MThd: length 6, format 0, 1 track, division 960 (0x03C0)
        'M', 'T', 'h', 'd', 0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x01, 0x03, 0xC0,
        // MTrk: length 60
        'M', 'T', 'r', 'k', 0x00, 0x00, 0x00, 0x3C,
        0x00, 0xFF, 0x03, 0x06, 'C', 'h', 'o', 'r', 'd', 's',  // track name "Chords"
        0x00, 0xFF, 0x58, 0x04, 0x04, 0x02, 0x18, 0x08,        // time signature 4/4
        0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20,              // tempo 500000 us/quarter = 120 BPM
        0x00, 0xFF, 0x59, 0x02, 0x00, 0x00,                    // key signature: C major
        0x00, 0x90, 0x30, 0x60,                                // note on  C (48) vel 96
        0x00, 0x90, 0x34, 0x60,                                // note on  E (52)
        0x00, 0x90, 0x37, 0x60,                                // note on  G (55)
        0x9E, 0x00, 0x80, 0x30, 0x00,                          // +3840: note off C
        0x00, 0x80, 0x34, 0x00,                                // note off E
        0x00, 0x80, 0x37, 0x00,                                // note off G
        0x00, 0xFF, 0x2F, 0x00,                                // end of track (tick 3840)
    };

    const Bytes actual = exportProgressionToMidi(p);
    CHECK(actual.size() == expected.size());
    CHECK(actual == expected);
}

// ------------------------------------------------------------------------------------------------
// Structure and the milestone progression
// ------------------------------------------------------------------------------------------------

TEST_CASE("exportProgressionToMidi: file structure of the milestone progression", "[midi][structure]") {
    const Bytes bytes = exportProgressionToMidi(makeDemoProgression());
    const auto file = parsed(bytes);  // strict parse: lengths, end-of-track, no trailing bytes

    CHECK(file.format == 0);
    CHECK(file.trackCount == 1);
    CHECK(file.division == 960);
    REQUIRE(file.tracks.size() == 1);
    CHECK_FALSE(file.tracks[0].usedRunningStatus);
}

TEST_CASE("exportProgressionToMidi: tempo, time signature, key signature are at tick 0", "[midi][meta]") {
    const auto file = parsed(exportProgressionToMidi(makeDemoProgression()));
    const auto& t = file.tracks[0];

    const auto ts = smf::metaEvents(t, 0x58);
    REQUIRE(ts.size() == 1);
    CHECK(ts[0]->tick == 0);
    CHECK(ts[0]->data == Bytes{4, 2, 24, 8});

    const auto tempo = smf::metaEvents(t, 0x51);
    REQUIRE(tempo.size() == 1);
    CHECK(tempo[0]->tick == 0);
    CHECK(tempoMicros(t) == 500000);

    const auto key = smf::metaEvents(t, 0x59);
    REQUIRE(key.size() == 1);
    CHECK(key[0]->tick == 0);
    CHECK(key[0]->data == Bytes{0x00, 0x00});

    const auto name = smf::metaEvents(t, 0x03);
    REQUIRE(name.size() == 1);
    CHECK(std::string(name[0]->data.begin(), name[0]->data.end()) == "Chords");

    const auto end = smf::metaEvents(t, 0x2F);
    REQUIRE(end.size() == 1);
    CHECK(end[0]->tick == 15360);                    // 4 bars
    CHECK(&t.events.back() == end[0]);               // last event in the track
}

TEST_CASE("exportProgressionToMidi: pitches, tick placement and lengths of the milestone file", "[midi][pitch][ticks][duration]") {
    const auto file = parsed(exportProgressionToMidi(makeDemoProgression()));
    const auto notes = smf::extractNotes(file.tracks[0]);

    struct Exp { int pitch; std::uint64_t start; };
    const Exp expected[12] = {
        {48, 0},     {52, 0},     {55, 0},
        {55, 3840},  {59, 3840},  {62, 3840},
        {57, 7680},  {60, 7680},  {64, 7680},
        {53, 11520}, {57, 11520}, {60, 11520},
    };
    REQUIRE(notes.size() == 12);
    for (int i = 0; i < 12; ++i) {
        INFO("note " << i);
        CHECK(notes[i].pitch == expected[i].pitch);
        CHECK(notes[i].start == expected[i].start);
        CHECK(notes[i].length == 3840);   // one full bar
        CHECK(notes[i].velocity == 96);
        CHECK(notes[i].channel == 0);
    }
}

TEST_CASE("note-off sorts before note-on at the same tick (adjacent identical chords retrigger)", "[midi][order]") {
    Progression p;
    p.blocks = {ChordBlock{kI, 0, 3840}, ChordBlock{kI, 3840, 3840}};
    const auto file = parsed(exportProgressionToMidi(p));
    const auto& t = file.tracks[0];

    std::size_t lastOff = 0, firstOn = SIZE_MAX;
    int offs = 0, ons = 0;
    for (const auto& e : t.events) {
        if (e.isMeta || e.tick != 3840) continue;
        if (e.kind() == 0x80) { lastOff = std::max(lastOff, e.fileOrder); ++offs; }
        if (e.kind() == 0x90) { firstOn = std::min(firstOn, e.fileOrder); ++ons; }
    }
    CHECK(offs == 3);
    CHECK(ons == 3);
    CHECK(lastOff < firstOn);

    // and no stuck or overlapping notes: extraction would throw otherwise
    CHECK(smf::extractNotes(t).size() == 6);
}

TEST_CASE("exported notes equal the engine's notes (one engine for playback and export)", "[midi][engine]") {
    const auto p = makeDemoProgression();
    const auto engine = generateNotes(p);
    const auto file = parsed(exportProgressionToMidi(p));
    const auto parsedNotes = smf::extractNotes(file.tracks[0]);

    REQUIRE(parsedNotes.size() == engine.size());
    for (std::size_t i = 0; i < engine.size(); ++i) {
        CHECK(parsedNotes[i].pitch == engine[i].pitch);
        CHECK(parsedNotes[i].start == static_cast<std::uint64_t>(engine[i].startTick));
        CHECK(parsedNotes[i].length == static_cast<std::uint64_t>(engine[i].lengthTicks));
        CHECK(parsedNotes[i].velocity == engine[i].velocity);
        CHECK(parsedNotes[i].channel == engine[i].channel);
    }

    // exportProgressionToMidi is exactly generateNotes + buildMidiFile
    CHECK(exportProgressionToMidi(p) == buildMidiFile(p, engine));
}

TEST_CASE("exportProgressionToMidi: byte-for-byte deterministic", "[midi]") {
    const auto p = makeDemoProgression();
    CHECK(exportProgressionToMidi(p) == exportProgressionToMidi(p));
}

// ------------------------------------------------------------------------------------------------
// Tempo, time signature, key signature
// ------------------------------------------------------------------------------------------------

TEST_CASE("tempo is written as microseconds per quarter note", "[midi][meta][tempo]") {
    struct Case { double bpm; std::uint32_t micros; };
    const Case cases[] = {
        {120.0, 500000}, {60.0, 1000000}, {90.0, 666667}, {140.0, 428571},
        {20.0, 3000000}, {999.0, 60060},  {100.0, 600000},
    };
    for (const auto& c : cases) {
        INFO("bpm " << c.bpm);
        auto p = makeDemoProgression();
        p.tempoBpm = c.bpm;
        const auto file = parsed(exportProgressionToMidi(p));
        CHECK(tempoMicros(file.tracks[0]) == c.micros);
    }
}

TEST_CASE("time signature meta event: numerator and power-of-two denominator", "[midi][meta][timesig]") {
    struct Case { int num, den; std::uint8_t denPow; };
    const Case cases[] = {{4, 4, 2}, {3, 4, 2}, {6, 8, 3}, {7, 8, 3}, {2, 2, 1}, {5, 4, 2}, {12, 8, 3}, {1, 1, 0}, {3, 16, 4}};
    for (const auto& c : cases) {
        INFO(c.num << "/" << c.den);
        auto p = makeDemoProgression();
        p.timeSignature = TimeSignature{c.num, c.den};
        p.blocks.clear();
        p.blocks.push_back(ChordBlock{kI, 0, p.timeSignature.ticksPerBar()});
        const auto file = parsed(exportProgressionToMidi(p));
        const auto ts = smf::metaEvents(file.tracks[0], 0x58);
        REQUIRE(ts.size() == 1);
        CHECK(ts[0]->tick == 0);
        CHECK(ts[0]->data[0] == c.num);
        CHECK(ts[0]->data[1] == c.denPow);

        // end-of-track lands exactly on the bar line
        const auto end = smf::metaEvents(file.tracks[0], 0x2F);
        CHECK(end[0]->tick == static_cast<std::uint64_t>(p.timeSignature.ticksPerBar()));
    }
}

TEST_CASE("midiKeySignatureSharps: all twelve major keys and relative minors", "[midi][meta][key]") {
    //                          C   Db  D   Eb  E   F   F#  G   Ab  A   Bb  B
    const int majors[12] = {0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5};
    for (int tonic = 0; tonic < 12; ++tonic) {
        INFO("major tonic " << tonic);
        CHECK(midiKeySignatureSharps(Key{tonic, Mode::Major}) == majors[tonic]);
        // a minor key shares its signature with the major key a minor third above
        CHECK(midiKeySignatureSharps(Key{tonic, Mode::Minor}) == majors[(tonic + 3) % 12]);
    }
    CHECK(midiKeySignatureSharps(Key{9, Mode::Minor}) == 0);    // A minor
    CHECK(midiKeySignatureSharps(Key{4, Mode::Minor}) == 1);    // E minor
    CHECK(midiKeySignatureSharps(Key{2, Mode::Minor}) == -1);   // D minor
    CHECK(midiKeySignatureSharps(Key{0, Mode::Minor}) == -3);   // C minor
    CHECK_THROWS_AS(midiKeySignatureSharps(Key{12, Mode::Major}), std::invalid_argument);
}

TEST_CASE("key signature meta event bytes (flats are two's complement)", "[midi][meta][key]") {
    struct Case { int tonic; Mode mode; std::uint8_t sharps; std::uint8_t minor; };
    const Case cases[] = {
        {0, Mode::Major, 0x00, 0}, {7, Mode::Major, 0x01, 0}, {2, Mode::Major, 0x02, 0},
        {5, Mode::Major, 0xFF, 0}, {10, Mode::Major, 0xFE, 0}, {1, Mode::Major, 0xFB, 0},
        {6, Mode::Major, 0x06, 0}, {9, Mode::Minor, 0x00, 1}, {4, Mode::Minor, 0x01, 1},
        {2, Mode::Minor, 0xFF, 1}, {0, Mode::Minor, 0xFD, 1},
    };
    for (const auto& c : cases) {
        INFO("tonic " << c.tonic << (c.mode == Mode::Minor ? " minor" : " major"));
        auto p = makeDemoProgression();
        p.key = Key{c.tonic, c.mode};
        const auto file = parsed(exportProgressionToMidi(p));
        const auto key = smf::metaEvents(file.tracks[0], 0x59);
        REQUIRE(key.size() == 1);
        CHECK(key[0]->data == Bytes{c.sharps, c.minor});
    }
}

TEST_CASE("export options: track name and key signature are optional", "[midi][options]") {
    const auto p = makeDemoProgression();
    MidiExportOptions o;
    o.trackName.clear();
    o.writeKeySignature = false;
    const auto file = parsed(exportProgressionToMidi(p, {}, o));
    CHECK(smf::metaEvents(file.tracks[0], 0x03).empty());
    CHECK(smf::metaEvents(file.tracks[0], 0x59).empty());
    CHECK(smf::metaEvents(file.tracks[0], 0x58).size() == 1);
    CHECK(smf::metaEvents(file.tracks[0], 0x51).size() == 1);

    o.trackName = "Verse";
    const auto named = parsed(exportProgressionToMidi(p, {}, o));
    const auto name = smf::metaEvents(named.tracks[0], 0x03);
    REQUIRE(name.size() == 1);
    CHECK(std::string(name[0]->data.begin(), name[0]->data.end()) == "Verse");
}

// ------------------------------------------------------------------------------------------------
// Lengths, channels, and unusual timelines
// ------------------------------------------------------------------------------------------------

TEST_CASE("note-off tick reflects note length (release gap)", "[midi][duration]") {
    GenerationOptions g;
    g.releaseGapTicks = 120;
    const auto file = parsed(exportProgressionToMidi(singleBlock(0, 3840), g));
    const auto notes = smf::extractNotes(file.tracks[0]);
    REQUIRE(notes.size() == 3);
    for (const auto& n : notes) {
        CHECK(n.start == 0);
        CHECK(n.length == 3720);
    }
    // the clip still ends on the bar line, after the last note-off
    CHECK(smf::metaEvents(file.tracks[0], 0x2F)[0]->tick == 3840);
}

TEST_CASE("a chord shorter than a bar pads the clip to a whole bar", "[midi][ticks]") {
    const auto file = parsed(exportProgressionToMidi(singleBlock(0, 1920)));
    const auto notes = smf::extractNotes(file.tracks[0]);
    REQUIRE(notes.size() == 3);
    CHECK(notes[0].length == 1920);
    CHECK(smf::metaEvents(file.tracks[0], 0x2F)[0]->tick == 3840);
}

TEST_CASE("a leading rest is exported as a delta time", "[midi][ticks]") {
    const auto file = parsed(exportProgressionToMidi(singleBlock(1920, 1920)));
    const auto notes = smf::extractNotes(file.tracks[0]);
    REQUIRE(notes.size() == 3);
    for (const auto& n : notes) {
        CHECK(n.start == 1920);
        CHECK(n.length == 1920);
    }
}

TEST_CASE("large tick values use multi-byte delta times", "[midi][vlq][ticks]") {
    for (Tick bars : {5, 300, 700}) {  // 19200 (3-byte VLQ), 1152000 (3-byte), 2688000 (4-byte)
        const Tick start = 3840 * bars;
        INFO("start tick " << start);
        const auto file = parsed(exportProgressionToMidi(singleBlock(start, 3840)));
        const auto notes = smf::extractNotes(file.tracks[0]);
        REQUIRE(notes.size() == 3);
        for (const auto& n : notes) {
            CHECK(n.start == static_cast<std::uint64_t>(start));
            CHECK(n.length == 3840);
        }
        CHECK(smf::metaEvents(file.tracks[0], 0x2F)[0]->tick == static_cast<std::uint64_t>(start + 3840));
    }
}

TEST_CASE("channel is encoded in the status byte", "[midi][channel]") {
    GenerationOptions g;
    g.channel = 9;
    const Bytes bytes = exportProgressionToMidi(singleBlock(0, 3840), g);
    const auto file = parsed(bytes);
    for (const auto& e : file.tracks[0].events) {
        if (e.isMeta) continue;
        CHECK(e.channel() == 9);
        CHECK((e.status == 0x99 || e.status == 0x89));
    }
    for (const auto& n : smf::extractNotes(file.tracks[0])) CHECK(n.channel == 9);
}

TEST_CASE("empty progression exports a valid, empty clip", "[midi]") {
    const auto file = parsed(exportProgressionToMidi(Progression{}));
    CHECK(smf::extractNotes(file.tracks[0]).empty());
    CHECK(smf::metaEvents(file.tracks[0], 0x2F)[0]->tick == 0);
    CHECK(smf::metaEvents(file.tracks[0], 0x51).size() == 1);
}

// ------------------------------------------------------------------------------------------------
// Error handling
// ------------------------------------------------------------------------------------------------

TEST_CASE("buildMidiFile rejects invalid progressions and notes", "[midi][errors]") {
    const auto p = makeDemoProgression();

    Progression overlapping;
    overlapping.blocks = {ChordBlock{kI, 0, 3840}, ChordBlock{kV, 100, 3840}};
    CHECK_THROWS_AS(buildMidiFile(overlapping, {}), std::invalid_argument);
    CHECK_THROWS_AS(exportProgressionToMidi(overlapping), std::invalid_argument);

    const NoteEvent good{60, 0, 960, 96, 0};
    CHECK_NOTHROW(buildMidiFile(p, {good}));

    auto bad = [&](NoteEvent n) { CHECK_THROWS_AS(buildMidiFile(p, {n}), std::invalid_argument); };
    { auto n = good; n.pitch = 128;    bad(n); }
    { auto n = good; n.pitch = -1;     bad(n); }
    { auto n = good; n.velocity = 0;   bad(n); }
    { auto n = good; n.velocity = 128; bad(n); }
    { auto n = good; n.channel = 16;   bad(n); }
    { auto n = good; n.channel = -1;   bad(n); }
    { auto n = good; n.startTick = -1; bad(n); }
    { auto n = good; n.lengthTicks = 0; bad(n); }

    auto tempo = p;
    tempo.tempoBpm = 5.0;
    CHECK_THROWS_AS(buildMidiFile(tempo, {}), std::invalid_argument);
}

// ------------------------------------------------------------------------------------------------
// Property test: random progressions survive generate -> export -> parse unchanged
// ------------------------------------------------------------------------------------------------

TEST_CASE("property: random progressions round-trip through the independent parser", "[midi][property]") {
    std::mt19937 rng(0xC0FFEE);  // fixed seed: failures are reproducible
    auto pick = [&](unsigned n) { return static_cast<int>(rng() % n); };

    const int denominators[] = {2, 4, 8, 16};
    for (int iteration = 0; iteration < 300; ++iteration) {
        Progression p;
        p.key = Key{pick(12), pick(2) == 0 ? Mode::Major : Mode::Minor};
        p.tempoBpm = 40 + pick(200);
        p.timeSignature = TimeSignature{1 + pick(12), denominators[pick(4)]};

        Tick cursor = 3840 * pick(3) + 60 * pick(5);  // sometimes starts after a rest
        const int blockCount = 1 + pick(8);
        for (int b = 0; b < blockCount; ++b) {
            ChordSpec c;
            c.degree = 1 + pick(7);
            c.accidental = pick(3) - 1;
            c.quality = static_cast<Quality>(pick(4));
            c.seventh = static_cast<Seventh>(pick(4));
            c.inversion = pick(c.seventh == Seventh::None ? 3 : 4);
            const Tick duration = 60 * (1 + pick(200));
            p.blocks.push_back(ChordBlock{c, cursor, duration});
            cursor += duration + (pick(4) == 0 ? 60 * pick(20) : 0);  // occasional gap
        }

        GenerationOptions g;
        g.velocity = 1 + pick(127);
        g.channel = pick(16);
        g.releaseGapTicks = pick(3) == 0 ? 30 * pick(10) : 0;

        INFO("iteration " << iteration);
        REQUIRE(p.validate().empty());

        const auto engine = generateNotes(p, g);
        const Bytes bytes = exportProgressionToMidi(p, g);
        const auto file = smf::parse(bytes);  // throws on any structural problem
        REQUIRE(file.tracks.size() == 1);
        CHECK_FALSE(file.tracks[0].usedRunningStatus);

        const auto back = smf::extractNotes(file.tracks[0]);
        REQUIRE(back.size() == engine.size());
        for (std::size_t i = 0; i < engine.size(); ++i) {
            REQUIRE(back[i].pitch == engine[i].pitch);
            REQUIRE(back[i].start == static_cast<std::uint64_t>(engine[i].startTick));
            REQUIRE(back[i].length == static_cast<std::uint64_t>(engine[i].lengthTicks));
            REQUIRE(back[i].velocity == engine[i].velocity);
            REQUIRE(back[i].channel == engine[i].channel);
        }

        const auto end = smf::metaEvents(file.tracks[0], 0x2F);
        REQUIRE(end.size() == 1);
        REQUIRE(end[0]->tick >= static_cast<std::uint64_t>(p.lengthTicks()));
        REQUIRE(end[0]->tick % static_cast<std::uint64_t>(p.timeSignature.ticksPerBar()) == 0);
    }
}
