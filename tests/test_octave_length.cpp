// Project length (clip length) and chord octaves: voicing, export, saved state, editing rules.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/ChordEdit.h>
#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>
#include <chordcore/Serialization.h>
#include <chordcore/Timeline.h>

#include "support/SmfParser.h"

#include <algorithm>
#include <set>
#include <stdexcept>

using namespace chordcore;

namespace {

std::vector<int> pitchesOf(const std::vector<NoteEvent>& notes) {
    std::vector<int> out;
    for (const auto& n : notes) out.push_back(n.pitch);
    return out;
}

std::uint64_t endOfTrackTick(const std::vector<std::uint8_t>& bytes) {
    const auto file = smf::parse(bytes);
    const auto& events = file.tracks.at(0).events;
    REQUIRE(events.back().isMeta);
    REQUIRE(events.back().metaType == 0x2F);
    return events.back().tick;
}

}  // namespace

// ------------------------------------------------------------------------------------------ clip length

TEST_CASE("lengthTicks: automatic by default, never shorter than the requested bars", "[length]") {
    auto p = makeDemoProgression();  // four 4/4 bars
    CHECK(p.lengthBars == 0);
    CHECK(p.lengthTicks() == 15360);

    p.lengthBars = 8;
    CHECK(p.lengthTicks() == 8 * 3840);
    p.lengthBars = 16;
    CHECK(p.lengthTicks() == 16 * 3840);
    p.lengthBars = 2;  // shorter than the chords: the chords win
    CHECK(p.lengthTicks() == 15360);

    Progression empty;
    CHECK(empty.lengthTicks() == 0);
    empty.lengthBars = 4;
    CHECK(empty.lengthTicks() == 4 * 3840);

    Progression waltz;
    waltz.timeSignature = TimeSignature{3, 4};
    waltz.lengthBars = 8;
    CHECK(waltz.lengthTicks() == 8 * 2880);  // bars, not ticks: follows the time signature
}

TEST_CASE("exported clip is as long as the requested project length", "[length][midi]") {
    auto p = makeDemoProgression();
    CHECK(endOfTrackTick(exportProgressionToMidi(p)) == 15360);
    p.lengthBars = 8;
    CHECK(endOfTrackTick(exportProgressionToMidi(p)) == 8 * 3840);
    p.lengthBars = 16;
    CHECK(endOfTrackTick(exportProgressionToMidi(p)) == 16 * 3840);

    // The notes themselves do not change: only the end of the clip moves.
    const auto before = generateNotes(makeDemoProgression());
    CHECK(generateNotes(p) == before);
}

TEST_CASE("validate: length and octave ranges", "[length][octave]") {
    auto p = makeDemoProgression();
    for (int bars : {0, 1, 4, 8, 16, 32, 256}) { p.lengthBars = bars; CHECK(p.validate().empty()); }
    p.lengthBars = -1; CHECK_FALSE(p.validate().empty());
    p.lengthBars = 257; CHECK_FALSE(p.validate().empty());
    p.lengthBars = 0;

    for (int o : {-2, -1, 0, 1, 2}) { p.octave = o; CHECK(p.validate().empty()); }
    p.octave = 3; CHECK_FALSE(p.validate().empty());
    p.octave = -3; CHECK_FALSE(p.validate().empty());
    p.octave = 0;

    p.blocks[1].octave = 2; CHECK(p.validate().empty());
    p.blocks[1].octave = -3; CHECK_FALSE(p.validate().empty());
    p.blocks[1].octave = 3; CHECK_FALSE(p.validate().empty());
}

// ------------------------------------------------------------------------------------------ octaves

TEST_CASE("octave 0 leaves the default voicing untouched", "[octave]") {
    const auto notes = generateNotes(makeDemoProgression());
    REQUIRE_FALSE(notes.empty());
    // C major I: C3 E3 G3 (MIDI 48 52 55), the milestone-1 voicing.
    CHECK(notes[0].pitch == 48);
    CHECK(notes[1].pitch == 52);
    CHECK(notes[2].pitch == 55);
}

TEST_CASE("project octave shifts every note by exactly twelve semitones per octave", "[octave]") {
    const auto base = pitchesOf(generateNotes(makeDemoProgression()));
    for (int octave : {-2, -1, 1, 2}) {
        auto p = makeDemoProgression();
        p.octave = octave;
        const auto shifted = pitchesOf(generateNotes(p));
        REQUIRE(shifted.size() == base.size());
        for (std::size_t i = 0; i < base.size(); ++i) CHECK(shifted[i] == base[i] + 12 * octave);
    }
}

TEST_CASE("a chord's own octave moves only that chord", "[octave]") {
    auto p = makeDemoProgression();
    p.blocks[2].octave = 1;   // vi up an octave
    p.blocks[3].octave = -1;  // IV down an octave
    const auto base = generateNotes(makeDemoProgression());
    const auto notes = generateNotes(p);
    REQUIRE(notes.size() == base.size());
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const int expected = notes[i].startTick == 7680 ? 12 : notes[i].startTick == 11520 ? -12 : 0;
        INFO("note " << i);
        CHECK(notes[i].pitch == base[i].pitch + expected);
        CHECK(notes[i].startTick == base[i].startTick);
        CHECK(notes[i].lengthTicks == base[i].lengthTicks);
    }
}

TEST_CASE("project and chord octaves add up", "[octave]") {
    auto p = makeDemoProgression();
    p.octave = 1;
    p.blocks[0].octave = 1;
    p.blocks[1].octave = -1;
    const auto base = generateNotes(makeDemoProgression());
    const auto notes = generateNotes(p);
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const int expected = notes[i].startTick == 0 ? 24 : notes[i].startTick == 3840 ? 0 : 12;
        CHECK(notes[i].pitch == base[i].pitch + expected);
    }
}

TEST_CASE("octave shifts keep every chord inside MIDI range at the extremes (all keys, chords, inversions)", "[octave][property]") {
    // The +-2 + +-2 limit is only safe if no voicing can leave 0..127. Check it exhaustively.
    for (int tonic = 0; tonic < 12; ++tonic)
    for (int mode = 0; mode < 2; ++mode)
    for (int degree = 1; degree <= 7; ++degree)
    for (int accidental = -1; accidental <= 1; ++accidental)
    for (int q = 0; q < 4; ++q)
    for (int s = 0; s < 4; ++s)
    for (int inversion = 0; inversion <= 3; ++inversion)
    for (int sus = 0; sus < 3; ++sus) {
        const ChordSpec chord{degree, accidental, static_cast<Quality>(q), static_cast<Seventh>(s), inversion, static_cast<Suspension>(sus)};
        if (!chord.isValid()) continue;
        for (int total : {-4, 4}) {
            Progression p;
            p.key = Key{tonic, mode ? Mode::Minor : Mode::Major};
            p.octave = total / 2;
            p.blocks.push_back(ChordBlock{chord, 0, 960, total / 2});
            REQUIRE(p.validate().empty());
            const auto notes = generateNotes(p);  // throws std::out_of_range if a pitch leaves 0..127
            for (const auto& n : notes) {
                REQUIRE(n.pitch >= 0);
                REQUIRE(n.pitch <= 127);
            }
        }
    }
}

TEST_CASE("exported MIDI carries the octaves (one engine for playback and export)", "[octave][midi]") {
    auto p = makeDemoProgression();
    p.octave = -1;
    p.blocks[1].octave = 2;
    const auto engine = generateNotes(p);
    const auto file = smf::parse(exportProgressionToMidi(p));
    const auto notes = smf::extractNotes(file.tracks.at(0));
    REQUIRE(notes.size() == engine.size());
    for (std::size_t i = 0; i < notes.size(); ++i) CHECK(notes[i].pitch == engine[i].pitch);
    CHECK(notes.front().pitch == 36);  // I down an octave: C2
}

// ------------------------------------------------------------------------------------------ saved state

TEST_CASE("state: default length/octave leave the saved bytes unchanged", "[state][octave][length]") {
    const auto text = serializeProgression(makeDemoProgression());
    CHECK(text.find("octave") == std::string::npos);
    CHECK(text.find("lengthBars") == std::string::npos);
}

TEST_CASE("state: length and octaves round-trip and appear only when set", "[state][octave][length]") {
    auto p = makeDemoProgression();
    p.octave = -1;
    p.lengthBars = 8;
    p.blocks[1].octave = 2;
    p.blocks[3].octave = -2;
    const auto text = serializeProgression(p);
    CHECK(text.find("\"octave\":-1,\"lengthBars\":8,\"blocks\"") != std::string::npos);
    CHECK(text.find("\"startTick\":3840,\"durationTicks\":3840,\"octave\":2}") != std::string::npos);
    CHECK(text.find("\"startTick\":0,\"durationTicks\":3840}") != std::string::npos);  // a chord at octave 0 has no field

    const auto back = deserializeProgression(text);
    REQUIRE(back.ok);
    CHECK(back.progression == p);
    CHECK(serializeProgression(back.progression) == text);
}

TEST_CASE("state: a file without the new fields still loads with defaults", "[state][octave][length][compat]") {
    // The version-1 golden document written before these options existed.
    const std::string old =
        "{\"format\":\"chordsketch-state\",\"version\":1,\"key\":{\"tonic\":0,\"mode\":\"major\"},\"tempoBpm\":120,"
        "\"timeSignature\":{\"numerator\":4,\"denominator\":4},\"blocks\":["
        "{\"degree\":1,\"accidental\":0,\"quality\":\"major\",\"seventh\":\"none\",\"inversion\":0,\"suspension\":\"none\",\"startTick\":0,\"durationTicks\":3840}]}";
    const auto r = deserializeProgression(old);
    REQUIRE(r.ok);
    CHECK(r.progression.octave == 0);
    CHECK(r.progression.lengthBars == 0);
    CHECK(r.progression.blocks[0].octave == 0);
}

TEST_CASE("state: bad length/octave values are rejected", "[state][octave][length][errors]") {
    auto bad = [](const std::string& field, const std::string& value, bool onBlock) {
        auto p = makeDemoProgression();
        auto text = serializeProgression(p);
        const std::string needle = onBlock ? "\"durationTicks\":3840}" : "\"blocks\":";
        const auto pos = text.find(needle);
        REQUIRE(pos != std::string::npos);
        if (onBlock) text.replace(pos, needle.size(), "\"durationTicks\":3840,\"" + field + "\":" + value + "}");
        else text.insert(pos, "\"" + field + "\":" + value + ",");
        INFO(text.substr(0, 200));
        const auto r = deserializeProgression(text);
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.error.empty());
    };
    bad("octave", "3", false);
    bad("octave", "-3", false);
    bad("octave", "0.5", false);
    bad("octave", "\"up\"", false);
    bad("lengthBars", "300", false);
    bad("lengthBars", "-1", false);
    bad("lengthBars", "8.5", false);
    bad("octave", "5", true);
    bad("octave", "\"x\"", true);
}

// ------------------------------------------------------------------------------------------ editing rules

TEST_CASE("shiftBlockOctave: steps, limits, bad index", "[octave][timeline]") {
    auto p = makeDemoProgression();
    CHECK(shiftBlockOctave(p, 0, +1).ok);
    CHECK(p.blocks[0].octave == 1);
    CHECK(shiftBlockOctave(p, 0, +1).ok);
    CHECK(p.blocks[0].octave == 2);
    const auto r = shiftBlockOctave(p, 0, +1);
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("highest") != std::string::npos);
    CHECK(p.blocks[0].octave == 2);

    CHECK(shiftBlockOctave(p, 1, -2).ok);
    CHECK(p.blocks[1].octave == -2);
    const auto low = shiftBlockOctave(p, 1, -1);
    CHECK_FALSE(low.ok);
    CHECK(low.error.find("lowest") != std::string::npos);

    CHECK_FALSE(shiftBlockOctave(p, 9, 1).ok);
    CHECK(p.validate().empty());
}

TEST_CASE("split and duplicate keep a chord's octave", "[octave][timeline]") {
    auto p = makeDemoProgression();
    p.blocks[0].octave = 1;
    REQUIRE(splitBlock(p, 0, 1920, 960).ok);
    CHECK(p.blocks[0].octave == 1);
    CHECK(p.blocks[1].octave == 1);

    auto q = makeDemoProgression();
    q.blocks[3].octave = -2;
    REQUIRE(duplicateBlock(q, 3).ok);
    CHECK(q.blocks[4].octave == -2);
    CHECK(q.validate().empty());
}

TEST_CASE("a newly added chord starts at octave 0; move and resize keep the octave", "[octave][timeline]") {
    Progression p;
    p.octave = 1;
    REQUIRE(addBlock(p, diatonicChord(Mode::Major, 1), 0, 960).ok);
    CHECK(p.blocks[0].octave == 0);  // the project octave still applies; the chord has no extra shift

    p.blocks[0].octave = -1;
    REQUIRE(moveBlock(p, 0, 3840, 960).ok);
    REQUIRE(resizeBlockEnd(p, 0, 3840 + 1920, 960).ok);
    REQUIRE(resizeBlockStart(p, 0, 3840 + 960, 960).ok);
    CHECK(p.blocks[0].octave == -1);
}
