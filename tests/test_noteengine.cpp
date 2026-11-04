// Note engine: progression -> NoteEvents. Pitch, start tick and length are asserted exactly.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>

#include <algorithm>
#include <stdexcept>
#include <vector>

using namespace chordcore;

namespace {

const ChordSpec kI{1, 0, Quality::Major, Seventh::None, 0};
const ChordSpec kV{5, 0, Quality::Major, Seventh::None, 0};

NoteEvent note(int pitch, Tick start, Tick length, int velocity = 96, int channel = 0) {
    return NoteEvent{pitch, start, length, velocity, channel};
}

}  // namespace

TEST_CASE("generateNotes: the four-chord milestone progression, exactly", "[engine][pitch][duration][ticks]") {
    const auto notes = generateNotes(makeDemoProgression());

    // I - V - vi - IV in C major, 120 BPM 4/4, one bar (3840 ticks) per chord. Sorted by (tick, pitch).
    const std::vector<NoteEvent> expected = {
        note(48, 0, 3840),     note(52, 0, 3840),     note(55, 0, 3840),      // C  E  G
        note(55, 3840, 3840),  note(59, 3840, 3840),  note(62, 3840, 3840),   // G  B  D
        note(57, 7680, 3840),  note(60, 7680, 3840),  note(64, 7680, 3840),   // A  C  E
        note(53, 11520, 3840), note(57, 11520, 3840), note(60, 11520, 3840),  // F  A  C
    };
    REQUIRE(notes.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        INFO("note index " << i);
        CHECK(notes[i] == expected[i]);
    }
}

TEST_CASE("generateNotes: output is sorted by (startTick, pitch)", "[engine]") {
    const auto notes = generateNotes(makeDemoProgression());
    CHECK(std::is_sorted(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch;
    }));

    // Blocks stored out of time order must still give sorted output.
    Progression p;
    p.blocks = {ChordBlock{kV, 3840, 3840}, ChordBlock{kI, 0, 3840}};
    const auto out = generateNotes(p);
    REQUIRE(out.size() == 6);
    CHECK(out.front().startTick == 0);
    CHECK(out.back().startTick == 3840);
}

TEST_CASE("generateNotes: deterministic", "[engine]") {
    const auto p = makeDemoProgression();
    CHECK(generateNotes(p) == generateNotes(p));
}

TEST_CASE("generateNotes: durations follow the block, not the bar", "[engine][duration][ticks]") {
    // dotted half (3 beats) then a quarter note: ticks 0..2880 and 2880..3840
    Progression p;
    p.blocks = {ChordBlock{kI, 0, 2880}, ChordBlock{kV, 2880, 960}};
    const auto notes = generateNotes(p);
    REQUIRE(notes.size() == 6);
    for (int i = 0; i < 3; ++i) {
        CHECK(notes[i].startTick == 0);
        CHECK(notes[i].lengthTicks == 2880);
    }
    for (int i = 3; i < 6; ++i) {
        CHECK(notes[i].startTick == 2880);
        CHECK(notes[i].lengthTicks == 960);
    }
}

TEST_CASE("generateNotes: rests are preserved (start ticks come from the block)", "[engine][ticks]") {
    Progression p;
    p.blocks = {ChordBlock{kI, 1920, 1920}};  // two-beat rest, then a half-note chord
    const auto notes = generateNotes(p);
    REQUIRE(notes.size() == 3);
    for (const auto& n : notes) {
        CHECK(n.startTick == 1920);
        CHECK(n.lengthTicks == 1920);
    }
}

TEST_CASE("generateNotes: odd time signatures use the block ticks unchanged", "[engine][ticks]") {
    Progression p;
    p.timeSignature = TimeSignature{6, 8};
    const Tick bar = p.timeSignature.ticksPerBar();  // 2880
    p.blocks = {ChordBlock{kI, 0, bar}, ChordBlock{kV, bar, bar}};
    const auto notes = generateNotes(p);
    REQUIRE(notes.size() == 6);
    CHECK(notes[0].startTick == 0);
    CHECK(notes[0].lengthTicks == 2880);
    CHECK(notes[3].startTick == 2880);
    CHECK(notes[3].lengthTicks == 2880);
}

TEST_CASE("generateNotes: release gap shortens the note, never moves the start", "[engine][duration]") {
    GenerationOptions o;
    o.releaseGapTicks = 120;
    const auto notes = generateNotes(makeDemoProgression(), o);
    REQUIRE(notes.size() == 12);
    for (std::size_t i = 0; i < notes.size(); ++i) {
        CHECK(notes[i].startTick == static_cast<Tick>(3840 * (i / 3)));
        CHECK(notes[i].lengthTicks == 3720);
    }

    SECTION("gap longer than the block clamps to one tick") {
        Progression p;
        p.blocks = {ChordBlock{kI, 0, 100}};
        o.releaseGapTicks = 500;
        const auto out = generateNotes(p, o);
        REQUIRE(out.size() == 3);
        for (const auto& n : out) CHECK(n.lengthTicks == 1);
    }
}

TEST_CASE("generateNotes: velocity, channel and voicing options pass through", "[engine]") {
    GenerationOptions o;
    o.velocity = 80;
    o.channel = 9;
    o.voicing.rootBase = 36;
    Progression p;
    p.blocks = {ChordBlock{kI, 0, 3840}};
    const auto notes = generateNotes(p, o);
    REQUIRE(notes.size() == 3);
    CHECK(notes[0] == note(36, 0, 3840, 80, 9));
    CHECK(notes[1] == note(40, 0, 3840, 80, 9));
    CHECK(notes[2] == note(43, 0, 3840, 80, 9));
}

TEST_CASE("generateNotes: chord size follows the chord type", "[engine]") {
    Progression p;
    p.blocks = {
        ChordBlock{ChordSpec{5, 0, Quality::Major, Seventh::Minor, 0}, 0, 3840},
        ChordBlock{ChordSpec{1, 0, Quality::Major, Seventh::None, 1}, 3840, 3840},
    };
    const auto notes = generateNotes(p);
    REQUIRE(notes.size() == 7);  // 4 + 3
    CHECK(notes[0].pitch == 55);
    CHECK(notes[3].pitch == 65);
    CHECK(notes[4].pitch == 52);  // first-inversion C: E G C
    CHECK(notes[6].pitch == 60);
}

TEST_CASE("generateNotes: suspended chord notes and ticks", "[engine][sus]") {
    Progression p;
    p.blocks = {
        ChordBlock{ChordSpec{5, 0, Quality::Major, Seventh::None, 0, Suspension::Sus4}, 0, 1920},
        ChordBlock{ChordSpec{5, 0, Quality::Major, Seventh::None, 0, Suspension::None}, 1920, 1920},
    };
    const auto notes = generateNotes(p);
    REQUIRE(notes.size() == 6);
    CHECK(notes[0] == note(55, 0, 1920));
    CHECK(notes[1] == note(60, 0, 1920));    // the sus4 note (C) replaces B
    CHECK(notes[2] == note(62, 0, 1920));
    CHECK(notes[3] == note(55, 1920, 1920));
    CHECK(notes[4] == note(59, 1920, 1920)); // resolution to the major third
    CHECK(notes[5] == note(62, 1920, 1920));
}

TEST_CASE("generateNotes: empty progression yields no notes", "[engine]") {
    CHECK(generateNotes(Progression{}).empty());
}

TEST_CASE("generateNotes: rejects invalid input", "[engine]") {
    SECTION("overlapping blocks") {
        Progression p;
        p.blocks = {ChordBlock{kI, 0, 3840}, ChordBlock{kV, 100, 3840}};
        CHECK_THROWS_AS(generateNotes(p), std::invalid_argument);
    }
    SECTION("bad options") {
        const auto p = makeDemoProgression();
        GenerationOptions o;
        o.velocity = 0;
        CHECK_THROWS_AS(generateNotes(p, o), std::invalid_argument);
        o = {};
        o.velocity = 128;
        CHECK_THROWS_AS(generateNotes(p, o), std::invalid_argument);
        o = {};
        o.channel = -1;
        CHECK_THROWS_AS(generateNotes(p, o), std::invalid_argument);
        o = {};
        o.channel = 16;
        CHECK_THROWS_AS(generateNotes(p, o), std::invalid_argument);
        o = {};
        o.releaseGapTicks = -1;
        CHECK_THROWS_AS(generateNotes(p, o), std::invalid_argument);
    }
    SECTION("voicing that leaves the MIDI range") {
        GenerationOptions o;
        o.voicing.rootBase = 120;
        CHECK_THROWS_AS(generateNotes(makeDemoProgression(), o), std::out_of_range);
    }
}
