// Melody lane: data model validation, save/load (incl. old projects), edit operations, undo. Expected values by hand.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Document.h>
#include <chordcore/MelodyEdit.h>
#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>
#include <chordcore/Serialization.h>

#include "support/SmfParser.h"

#include <algorithm>
#include <stdexcept>

using namespace chordcore;

namespace {
Progression base() { return makeDemoProgression(); }
MelodyNote note(int pitch, Tick start, Tick dur, int vel = 90) {
    MelodyNote n;
    n.pitch = pitch;
    n.startTick = start;
    n.durationTicks = dur;
    n.velocity = vel;
    return n;
}
}  // namespace

TEST_CASE("melody: validation rules", "[melody]") {
    auto p = base();
    CHECK(p.validate().empty());
    p.melody = {note(60, 0, 480)};
    CHECK(p.validate().empty());
    p.melody = {note(128, 0, 480)};
    CHECK_FALSE(p.validate().empty());
    p.melody = {note(60, -1, 480)};
    CHECK_FALSE(p.validate().empty());
    p.melody = {note(60, 0, 0)};
    CHECK_FALSE(p.validate().empty());
    p.melody = {note(60, 0, 480, 0)};
    CHECK_FALSE(p.validate().empty());
    p.melody = {note(60, 0, 480, 128)};
    CHECK_FALSE(p.validate().empty());
    p.melody = {note(60, 0, 480), note(60, 479, 480)};  // same pitch overlap
    CHECK_FALSE(p.validate().empty());
    p.melody = {note(60, 0, 480), note(60, 480, 480)};  // touching is fine
    CHECK(p.validate().empty());
    p.melody = {note(60, 0, 480), note(62, 0, 480)};  // other pitch may overlap
    CHECK(p.validate().empty());
    p.melody.clear();
    p.melodySettings.melodyChannel = 17;
    CHECK_FALSE(p.validate().empty());
}

TEST_CASE("melody: extends project length and equality", "[melody]") {
    auto p = base();  // 4 bars
    CHECK(p.lengthTicks() == 4 * 3840);
    p.melody = {note(60, 5 * 3840, 100)};
    CHECK(p.melodyEndTick() == 5 * 3840 + 100);
    CHECK(p.lengthTicks() == 6 * 3840);
    auto q = base();
    CHECK(p != q);
    q.melody = p.melody;
    CHECK(p == q);
    q.melodySettings.muteMelody = true;
    CHECK(p != q);
}

TEST_CASE("melody: save and reload (test J)", "[melody]") {
    auto p = base();
    p.melody = {note(60, 0, 480, 100), note(64, 480, 960, 55)};
    p.melody[1].locked = true;
    p.melodySettings.mapping = ColourMapping::MajorCentric;
    p.melodySettings.palette = PaletteId::ColourBlindSafe;
    p.melodySettings.labels = LabelMode::Both;
    p.melodySettings.chordChannel = 3;
    p.melodySettings.melodyChannel = 5;
    p.melodySettings.muteChords = true;
    p.melodySettings.muteMelody = true;
    const auto text = serializeProgression(p);
    const auto r = deserializeProgression(text);
    REQUIRE(r.ok);
    CHECK(r.progression == p);
    CHECK(serializeProgression(r.progression) == text);
}

TEST_CASE("melody: old projects load and stay byte-identical", "[melody]") {
    const auto p = base();
    const auto text = serializeProgression(p);
    CHECK(text.find("melody") == std::string::npos);
    const auto r = deserializeProgression(text);
    REQUIRE(r.ok);
    CHECK(r.progression.melody.empty());
    CHECK(r.progression.melodySettings == MelodySettings{});
    CHECK(serializeProgression(r.progression) == text);
}

TEST_CASE("melody: malformed melody data is rejected", "[melody]") {
    auto p = base();
    p.melody = {note(60, 0, 480)};
    auto text = serializeProgression(p);
    const auto pos = text.find("\"pitch\":60");
    REQUIRE(pos != std::string::npos);
    auto bad = text;
    bad.replace(pos, 10, "\"pitch\":300");
    CHECK_FALSE(deserializeProgression(bad).ok);
    bad = text;
    bad.replace(pos, 10, "\"pitch\":\"x\"");
    CHECK_FALSE(deserializeProgression(bad).ok);
}

TEST_CASE("melody edit: add", "[melody]") {
    auto p = base();
    auto r = addMelodyNote(p, 64, 500, 480, 0, 80);  // snaps to 480
    REQUIRE(r.ok);
    REQUIRE(p.melody.size() == 1);
    CHECK(p.melody[0].startTick == 480);
    CHECK(p.melody[0].durationTicks == 480);
    CHECK(p.melody[0].velocity == 80);
    CHECK_FALSE(addMelodyNote(p, 64, 600, 120).ok);   // inside same pitch note
    CHECK(addMelodyNote(p, 65, 600, 120).ok);          // other pitch is fine
    CHECK_FALSE(addMelodyNote(p, 200, 0, 480).ok);
    // shortened by the next same-pitch note
    r = addMelodyNote(p, 64, 0, 120, 2000);
    REQUIRE(r.ok);
    CHECK(p.melody[r.index].startTick == 0);
    CHECK(p.melody[r.index].durationTicks == 480);
    CHECK(p.validate().empty());
    // sorted by start then pitch
    for (std::size_t i = 1; i < p.melody.size(); ++i) {
        CHECK((p.melody[i - 1].startTick < p.melody[i].startTick ||
               (p.melody[i - 1].startTick == p.melody[i].startTick && p.melody[i - 1].pitch <= p.melody[i].pitch)));
    }
    // velocity clamps
    CHECK(addMelodyNote(p, 70, 0, 480, 0, 999).ok);
    CHECK(p.melody[findMelodyNoteAt(p, 0, 70)].velocity == 127);
}

TEST_CASE("melody edit: move time and pitch", "[melody]") {
    auto p = base();
    p.melody = {note(60, 0, 480), note(64, 480, 480)};
    auto r = moveMelodyNotes(p, {0, 1}, 0, 960, 2, 480);
    REQUIRE(r.ok);
    CHECK(p.melody[0].startTick == 960);
    CHECK(p.melody[0].pitch == 62);
    CHECK(p.melody[1].startTick == 1440);
    CHECK(p.melody[1].pitch == 66);
    // clamp at tick 0 and the pitch range
    r = moveMelodyNotes(p, {0, 1}, 0, -5000, 500, 480);
    REQUIRE(r.ok);
    CHECK(p.melody[0].startTick == 0);
    CHECK(p.melody[1].startTick == 480);
    CHECK(p.melody[1].pitch == 127);
    CHECK(p.melody[0].pitch == 123);
    CHECK(p.validate().empty());
}

TEST_CASE("melody edit: move rejected on same-pitch overlap and leaves state", "[melody]") {
    auto p = base();
    p.melody = {note(60, 0, 480), note(60, 960, 480)};
    const auto before = p;
    CHECK_FALSE(moveMelodyNotes(p, {0}, 0, 800, 0, 1).ok);
    CHECK(p == before);
    CHECK_FALSE(moveMelodyNotes(p, {}, 0, 0, 0, 1).ok);
    CHECK_FALSE(moveMelodyNotes(p, {0, 0}, 0, 0, 0, 1).ok);
    CHECK_FALSE(moveMelodyNotes(p, {5}, 5, 0, 0, 1).ok);
}

TEST_CASE("melody edit: move re-sorts and reports new indices", "[melody]") {
    auto p = base();
    p.melody = {note(60, 0, 480), note(62, 480, 480), note(64, 960, 480)};
    const auto r = moveMelodyNotes(p, {0}, 0, 1440, 0, 480);
    REQUIRE(r.ok);
    REQUIRE(r.indices.size() == 1);
    CHECK(r.indices[0] == 2);
    CHECK(p.melody[2].pitch == 60);
    CHECK(p.melody[0].pitch == 62);
}

TEST_CASE("melody edit: resize end and start", "[melody]") {
    auto p = base();
    p.melody = {note(60, 480, 480), note(60, 1920, 480)};
    CHECK(resizeMelodyNoteEnd(p, 0, 1200, 240).ok);
    CHECK(p.melody[0].durationTicks == 720);  // 1200 snaps to 1200 -> end 1200 => dur 720
    CHECK(resizeMelodyNoteEnd(p, 0, 5000, 240).ok);
    CHECK(p.melody[0].endTick() == 1920);  // stopped by the next same-pitch note
    CHECK(resizeMelodyNoteEnd(p, 0, 0, 240).ok);
    CHECK(p.melody[0].durationTicks == 240);  // minimum one unit
    CHECK(resizeMelodyNoteStart(p, 1, 0, 240).ok);
    CHECK(p.melody[1].startTick == p.melody[0].endTick());  // stopped by the previous note
    CHECK(p.melody[1].endTick() == 2400);
    CHECK_FALSE(resizeMelodyNoteEnd(p, 9, 0, 1).ok);
    CHECK(p.validate().empty());
}

TEST_CASE("melody edit: delete, duplicate, paste", "[melody]") {
    auto p = base();
    p.melody = {note(60, 0, 480), note(62, 480, 480), note(64, 960, 480)};
    auto r = duplicateMelodyNotes(p, {0, 1});
    REQUIRE(r.ok);
    REQUIRE(p.melody.size() == 5);
    CHECK(r.indices.size() == 2);
    CHECK(p.melody[r.indices[0]].startTick == 960);  // group spans 0..960 -> copy starts at 960
    CHECK(p.validate().empty());

    auto q = base();
    q.melody = {note(60, 0, 480)};
    q.melody[0].locked = true;
    const auto clip = melodyClipboard(q, {0});
    REQUIRE(pasteMelodyNotes(q, clip, 960, 480).ok);
    REQUIRE(q.melody.size() == 2);
    CHECK(q.melody[1].startTick == 960);
    CHECK_FALSE(q.melody[1].locked);
    CHECK_FALSE(pasteMelodyNotes(q, clip, 960, 480).ok);  // already occupied
    CHECK_FALSE(pasteMelodyNotes(q, {}, 0, 1).ok);

    REQUIRE(deleteMelodyNotes(q, {0}).ok);
    CHECK(q.melody.size() == 1);
    CHECK(q.melody[0].startTick == 960);
    CHECK_FALSE(deleteMelodyNotes(q, {3}).ok);
}

TEST_CASE("melody edit: velocity and lock (test K data)", "[melody]") {
    auto p = base();
    p.melody = {note(60, 0, 480), note(62, 480, 480)};
    REQUIRE(setMelodyVelocity(p, {0, 1}, 300).ok);
    CHECK(p.melody[0].velocity == 127);
    REQUIRE(setMelodyVelocity(p, {1}, -4).ok);
    CHECK(p.melody[1].velocity == 1);
    REQUIRE(setMelodyLocked(p, {1}, true).ok);
    CHECK(p.melody[1].locked);
    CHECK_FALSE(p.melody[0].locked);
    REQUIRE(setMelodyLocked(p, {1}, false).ok);
    CHECK_FALSE(p.melody[1].locked);
}

TEST_CASE("melody edit: undo and redo through the document", "[melody]") {
    Document d(base());
    REQUIRE(d.edit("Add note", [](Progression& p) {
                 REQUIRE(addMelodyNote(p, 60, 0, 480).ok);
             }).ok);
    REQUIRE(d.edit("Lock", [](Progression& p) {
                 REQUIRE(setMelodyLocked(p, {0}, true).ok);
             }).ok);
    CHECK(d.progression().melody.size() == 1);
    CHECK(d.progression().melody[0].locked);
    REQUIRE(d.undo());
    CHECK_FALSE(d.progression().melody[0].locked);
    REQUIRE(d.undo());
    CHECK(d.progression().melody.empty());
    REQUIRE(d.redo());
    REQUIRE(d.redo());
    CHECK(d.progression().melody[0].locked);
    // an edit that makes the project invalid is rejected and changes nothing
    const auto before = d.progression();
    CHECK_FALSE(d.edit("Bad", [](Progression& p) { p.melody.push_back(p.melody[0]); }).ok);
    CHECK(d.progression() == before);
}

TEST_CASE("melody: project notes use separate channels and honour mutes (test F)", "[melody]") {
    auto p = base();
    p.melody = {note(72, 0, 480), note(74, 480, 480)};
    const auto all = generateProjectNotes(p);
    int chordNotes = 0, melodyNotes = 0;
    for (const auto& n : all) {
        if (n.channel == 0) ++chordNotes;
        else if (n.channel == 1) ++melodyNotes;
        else FAIL("unexpected channel " << n.channel);
    }
    CHECK(chordNotes == 12);  // four triads
    CHECK(melodyNotes == 2);
    CHECK(generateMelodyNotes(p).size() == 2);
    CHECK(generateMelodyNotes(p)[0].channel == 1);

    p.melodySettings.muteMelody = true;
    for (const auto& n : generateProjectNotes(p)) CHECK(n.channel == 0);
    p.melodySettings.muteMelody = false;
    p.melodySettings.muteChords = true;
    const auto onlyMelody = generateProjectNotes(p);
    REQUIRE(onlyMelody.size() == 2);
    for (const auto& n : onlyMelody) CHECK(n.channel == 1);

    p.melodySettings = MelodySettings{};
    p.melodySettings.chordChannel = 5;
    p.melodySettings.melodyChannel = 9;
    for (const auto& n : generateProjectNotes(p)) CHECK((n.channel == 4 || n.channel == 8));
    // the chord events are exactly what the chord generator produces
    p.melody.clear();
    GenerationOptions o;
    o.channel = 4;
    CHECK(generateProjectNotes(p) == generateNotes(p, o));
}

namespace {

Progression withMelody() {
    auto p = base();  // 4 bars, 3840 ticks each, C major I V vi IV
    p.melody = {note(72, 0, 480), note(74, 480, 480), note(76, 3840 + 960, 1920), note(79, 3 * 3840, 960)};
    return p;
}

std::string trackName(const smf::Track& t) {
    const auto names = smf::metaEvents(t, 0x03);
    if (names.empty()) return {};
    return std::string(names[0]->data.begin(), names[0]->data.end());
}

std::uint64_t endOf(const smf::Track& t) { return smf::metaEvents(t, 0x2F).at(0)->tick; }

}  // namespace

TEST_CASE("export: Chords.mid has only chords on the chord channel (test G)", "[melody][export]") {
    auto p = withMelody();
    ProjectExportOptions o;
    o.part = ExportPart::Chords;
    const auto file = smf::parse(exportProject(p, o));
    CHECK(file.format == 0);
    REQUIRE(file.tracks.size() == 1);
    CHECK(trackName(file.tracks[0]) == "Chords");
    const auto notes = smf::extractNotes(file.tracks[0]);
    CHECK(notes.size() == 12);
    for (const auto& n : notes) CHECK(n.channel == 0);
    CHECK(smf::metaEvents(file.tracks[0], 0x51).size() == 1);
}

TEST_CASE("export: Melody.mid has only melody, on channel 2 by default or 1 for compatibility", "[melody][export]") {
    auto p = withMelody();
    ProjectExportOptions o;
    o.part = ExportPart::Melody;
    auto file = smf::parse(exportProject(p, o));
    CHECK(file.format == 0);
    REQUIRE(file.tracks.size() == 1);
    CHECK(trackName(file.tracks[0]) == "Melody");
    auto notes = smf::extractNotes(file.tracks[0]);
    REQUIRE(notes.size() == 4);
    for (const auto& n : notes) CHECK(n.channel == 1);
    CHECK(notes[0].pitch == 72);
    CHECK(notes[0].length == 480);
    CHECK(notes[2].start == 3840 + 960);
    CHECK(notes[2].length == 1920);
    CHECK(smf::metaEvents(file.tracks[0], 0x51).size() == 1);  // standalone files carry tempo

    o.melodyOnChannel1 = true;
    file = smf::parse(exportProject(p, o));
    notes = smf::extractNotes(file.tracks[0]);
    for (const auto& n : notes) CHECK(n.channel == 0);
}

TEST_CASE("export: Combined.mid is Format 1 with shared tempo and named tracks", "[melody][export]") {
    const auto p = withMelody();
    const auto file = smf::parse(exportProject(p));
    CHECK(file.format == 1);
    REQUIRE(file.tracks.size() == 3);
    CHECK(file.division == 960);
    CHECK(smf::extractNotes(file.tracks[0]).empty());
    CHECK(smf::metaEvents(file.tracks[0], 0x51).size() == 1);
    CHECK(smf::metaEvents(file.tracks[0], 0x58).size() == 1);
    CHECK(smf::metaEvents(file.tracks[0], 0x59).size() == 1);
    CHECK(trackName(file.tracks[1]) == "Chords");
    CHECK(trackName(file.tracks[2]) == "Melody");
    for (std::size_t i = 1; i < 3; ++i) {
        CHECK(smf::metaEvents(file.tracks[i], 0x51).empty());
        CHECK(smf::metaEvents(file.tracks[i], 0x58).empty());
    }
    const auto chords = smf::extractNotes(file.tracks[1]);
    const auto melody = smf::extractNotes(file.tracks[2]);
    CHECK(chords.size() == 12);
    CHECK(melody.size() == 4);
    for (const auto& n : chords) CHECK(n.channel == 0);
    for (const auto& n : melody) CHECK(n.channel == 1);
    CHECK(endOf(file.tracks[0]) == endOf(file.tracks[1]));
    CHECK(endOf(file.tracks[1]) == endOf(file.tracks[2]));
    CHECK(endOf(file.tracks[0]) == static_cast<std::uint64_t>(p.lengthTicks()));
}

TEST_CASE("export: every part is the playback events (same representation)", "[melody][export]") {
    auto p = withMelody();
    p.melodySettings.chordChannel = 5;
    p.melodySettings.melodyChannel = 9;
    const auto playback = generateProjectNotes(p);
    const auto file = smf::parse(exportProject(p));
    auto chords = smf::extractNotes(file.tracks[1]);
    auto melody = smf::extractNotes(file.tracks[2]);
    REQUIRE(chords.size() + melody.size() == playback.size());
    std::vector<NoteEvent> fromFile;
    for (const auto& n : chords) fromFile.push_back({n.pitch, static_cast<Tick>(n.start), static_cast<Tick>(n.length), n.velocity, n.channel});
    for (const auto& n : melody) fromFile.push_back({n.pitch, static_cast<Tick>(n.start), static_cast<Tick>(n.length), n.velocity, n.channel});
    for (const auto& e : playback) {
        const auto it = std::find(fromFile.begin(), fromFile.end(), e);
        REQUIRE(it != fromFile.end());
        fromFile.erase(it);
    }
    CHECK(fromFile.empty());
    for (const auto& n : chords) CHECK(n.channel == 4);
    for (const auto& n : melody) CHECK(n.channel == 8);
}

TEST_CASE("export: parts are independent and ignore live mutes", "[melody][export]") {
    auto p = withMelody();
    ProjectExportOptions mel;
    mel.part = ExportPart::Melody;
    const auto before = exportProject(p, mel);
    p.blocks[0].chord = ChordSpec{4, 0, Quality::Major, Seventh::None, 0};  // change a chord
    CHECK(exportProject(p, mel) == before);                                  // melody file unchanged
    ProjectExportOptions ch;
    ch.part = ExportPart::Chords;
    const auto chordsBefore = exportProject(p, ch);
    p.melody.clear();
    CHECK(exportProject(p, ch) == chordsBefore);  // chord file unchanged when the melody is removed

    p = withMelody();
    p.melodySettings.muteChords = true;
    p.melodySettings.muteMelody = true;
    const auto file = smf::parse(exportProject(p));
    CHECK(smf::extractNotes(file.tracks[1]).size() == 12);
    CHECK(smf::extractNotes(file.tracks[2]).size() == 4);
}

TEST_CASE("export: bar range clips both parts and keeps the shared length", "[melody][export]") {
    auto p = withMelody();
    ProjectExportOptions o;
    o.firstBar = 2;
    o.lastBar = 2;
    const auto file = smf::parse(exportProject(p, o));
    const auto chords = smf::extractNotes(file.tracks[1]);
    const auto melody = smf::extractNotes(file.tracks[2]);
    CHECK(chords.size() == 3);  // bar 2 holds only the V chord
    REQUIRE(melody.size() == 1);
    CHECK(melody[0].pitch == 76);
    CHECK(melody[0].start == 960);  // 3840+960 shifted by one bar
    CHECK(melody[0].length == 1920);
    CHECK(endOf(file.tracks[0]) == 3840);
    CHECK(endOf(file.tracks[2]) == 3840);

    o.lastBar = 1;
    CHECK_THROWS_AS(exportProject(p, o), std::invalid_argument);
    o.firstBar = 0;
    o.lastBar = 300;
    CHECK_THROWS_AS(exportProject(p, o), std::invalid_argument);
}

TEST_CASE("export: file names and empty melody", "[melody][export]") {
    CHECK(std::string(exportFileName(ExportPart::Chords)) == "Chords.mid");
    CHECK(std::string(exportFileName(ExportPart::Melody)) == "Melody.mid");
    CHECK(std::string(exportFileName(ExportPart::Combined)) == "Combined.mid");
    const auto p = base();
    const auto file = smf::parse(exportProject(p));
    CHECK(file.tracks.size() == 3);
    CHECK(smf::extractNotes(file.tracks[2]).empty());
}
