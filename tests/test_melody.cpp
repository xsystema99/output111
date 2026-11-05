// Melody lane: data model validation, save/load (incl. old projects), edit operations, undo. Expected values by hand.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Document.h>
#include <chordcore/MelodyEdit.h>
#include <chordcore/Progression.h>
#include <chordcore/Serialization.h>

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
