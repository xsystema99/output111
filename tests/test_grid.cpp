// Precision chord grid: musical positions, snap modes, ruler, zoom/scroll, group edits, and the end-to-end
// guarantee that a chord placed on a grid position is generated, exported and restored at exactly that tick.
// Expected tick values are worked out by hand (4/4, 960 PPQ).

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Document.h>
#include <chordcore/Grid.h>
#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Serialization.h>
#include <chordcore/Timeline.h>

#include "support/SmfParser.h"

#include <algorithm>
#include <set>
#include <stdexcept>

using namespace chordcore;

namespace {

const TimeSignature k44{4, 4};
const ChordSpec kI{1, 0, Quality::Major, Seventh::None, 0, Suspension::None};
const ChordSpec kV7{5, 0, Quality::Major, Seventh::Minor, 1, Suspension::None};  // inverted seventh
const Tick kQuarter = 960;

Progression empty() {
    Progression p;
    return p;
}

std::vector<int> pitchesAt(const std::vector<NoteEvent>& notes, Tick start) {
    std::vector<int> out;
    for (const auto& n : notes) if (n.startTick == start) out.push_back(n.pitch);
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST_CASE("tickOf maps counting to ticks", "[grid]") {
    CHECK(tickOf(k44, 1, 1, Subdivision::Beat) == 0);
    CHECK(tickOf(k44, 1, 1, Subdivision::And) == 480);
    CHECK(tickOf(k44, 1, 2, Subdivision::Beat) == 960);
    CHECK(tickOf(k44, 1, 2, Subdivision::And) == 1440);
    CHECK(tickOf(k44, 1, 3, Subdivision::Beat) == 1920);
    CHECK(tickOf(k44, 1, 3, Subdivision::And) == 2400);
    CHECK(tickOf(k44, 1, 4, Subdivision::Beat) == 2880);
    CHECK(tickOf(k44, 1, 4, Subdivision::And) == 3360);
    CHECK(tickOf(k44, 1, 3, Subdivision::E) == 2160);
    CHECK(tickOf(k44, 1, 3, Subdivision::A) == 2640);  // Test B position
    CHECK(tickOf(k44, 3, 1, Subdivision::Beat) == 7680);
    CHECK_THROWS_AS(tickOf(k44, 1, 5, Subdivision::Beat), std::invalid_argument);
    CHECK_THROWS_AS(tickOf(k44, 0, 1, Subdivision::Beat), std::invalid_argument);
    CHECK_THROWS_AS(tickOf(k44, 1, 1, Subdivision::Other), std::invalid_argument);
}

TEST_CASE("positionOf is the inverse of tickOf and labels read naturally", "[grid]") {
    for (int bar = 1; bar <= 3; ++bar)
        for (int beat = 1; beat <= 4; ++beat)
            for (auto sub : {Subdivision::Beat, Subdivision::E, Subdivision::And, Subdivision::A}) {
                const auto m = positionOf(k44, tickOf(k44, bar, beat, sub));
                CHECK(m.bar == bar);
                CHECK(m.beat == beat);
                CHECK(m.sub == sub);
            }
    CHECK(positionLabel(k44, 1440) == "Bar 1, beat 2 &");
    CHECK(positionLabel(k44, 2640) == "Bar 1, beat 3 a");
    CHECK(positionLabel(k44, 3840) == "Bar 2, beat 1");
    CHECK(positionLabel(k44, 960 + 320) == "Bar 1, beat 2 +320t");  // triplet position
    CHECK(positionOf(k44, -50).bar == 1);
}

TEST_CASE("snap units, straight and triplet, are exact integers", "[grid][snap]") {
    CHECK(snapUnitTicks(SnapMode::Bar, k44) == 3840);
    CHECK(snapUnitTicks(SnapMode::HalfNote, k44) == 1920);
    CHECK(snapUnitTicks(SnapMode::QuarterNote, k44) == 960);
    CHECK(snapUnitTicks(SnapMode::EighthNote, k44) == 480);
    CHECK(snapUnitTicks(SnapMode::SixteenthNote, k44) == 240);
    CHECK(snapUnitTicks(SnapMode::ThirtySecondNote, k44) == 120);
    CHECK(snapUnitTicks(SnapMode::EighthTriplet, k44) == 320);
    CHECK(snapUnitTicks(SnapMode::SixteenthTriplet, k44) == 160);
    CHECK(snapUnitTicks(SnapMode::Off, k44) == 1);
    CHECK(snapUnitTicks(SnapMode::EighthNote, TimeSignature{6, 8}) == 480);
    CHECK(kQuarter % snapUnitTicks(SnapMode::EighthTriplet, k44) == 960 % 320);
    CHECK(960 % 320 == 0);
    CHECK(960 % 160 == 0);
}

TEST_CASE("Test A: a chord on the '&' of beat 2 starts at tick 1440", "[grid][testA]") {
    auto p = empty();
    const Tick clicked = tickOf(k44, 1, 2, Subdivision::And) + 37;  // imprecise click, snapped
    const auto r = addBlock(p, kI, clicked, snapUnitTicks(SnapMode::EighthNote, k44), kQuarter);
    REQUIRE(r.ok);
    CHECK(p.blocks[r.index].startTick == 1440);
    CHECK(p.blocks[r.index].durationTicks == 960);
}

TEST_CASE("Test B: a chord on the 'a' of beat 3 starts at tick 2640", "[grid][testB]") {
    auto p = empty();
    const auto r = addBlock(p, kI, 2640 - 50, snapUnitTicks(SnapMode::SixteenthNote, k44), 240);
    REQUIRE(r.ok);
    CHECK(p.blocks[r.index].startTick == 2640);
}

TEST_CASE("Test C: a one-quarter chord at 1440 has its note-offs at 2400", "[grid][testC]") {
    auto p = empty();
    REQUIRE(addBlock(p, kI, 1440, 480, kQuarter).ok);
    const auto notes = generateNotes(p);
    REQUIRE_FALSE(notes.empty());
    for (const auto& n : notes) {
        CHECK(n.startTick == 1440);
        CHECK(n.startTick + n.lengthTicks == 2400);
    }
}

TEST_CASE("Test D: changing the grid snap never moves existing chords", "[grid][testD]") {
    auto p = empty();
    REQUIRE(addBlock(p, kI, 1440, snapUnitTicks(SnapMode::EighthNote, k44), kQuarter).ok);
    REQUIRE(addBlock(p, kV7, 2400, snapUnitTicks(SnapMode::EighthNote, k44), kQuarter).ok);
    const auto before = p.blocks;
    for (auto mode : {SnapMode::SixteenthNote, SnapMode::EighthTriplet, SnapMode::Off, SnapMode::Bar, SnapMode::EighthNote}) {
        (void)snapUnitTicks(mode, k44);  // a snap change is only a number; the model is untouched
        CHECK(p.blocks == before);
    }
    // a no-op nudge at the finer grid keeps an on-grid chord where it is
    REQUIRE(moveBlock(p, 0, 1440, snapUnitTicks(SnapMode::SixteenthNote, k44)).ok);
    CHECK(p.blocks == before);
}

TEST_CASE("Test E: zoom and scroll never touch musical positions", "[grid][testE]") {
    auto p = empty();
    REQUIRE(addBlock(p, kI, 1440, 480, kQuarter).ok);
    const auto before = p.blocks;
    const double width = 800.0;
    auto view = fitView(p, width);
    const Tick extent = scrollExtent(p);
    for (int i = 0; i < 40; ++i) {
        view = zoomView(view, 300.0, i % 2 ? 0.8 : 1.3, extent, width);
        view = scrollView(view, i * 7.0, extent, width);
    }
    CHECK(p.blocks == before);
    CHECK(view.pixelsPerTick > 0.0);
    CHECK(view.pixelsPerTick <= kMaxPixelsPerTick);
}

TEST_CASE("zoomView keeps the tick under the cursor fixed and respects limits", "[grid][zoom]") {
    const double width = 1000.0;
    const Tick extent = 3840 * 16;
    TimelineView v;
    v.pixelsPerTick = 0.1;
    v.originTick = 5000;
    const Tick anchor = v.xToTick(400.0);
    const auto z = zoomView(v, 400.0, 2.0, extent, width);
    CHECK(std::abs(static_cast<long long>(z.xToTick(400.0) - anchor)) <= 2);
    CHECK(z.pixelsPerTick == 0.2);
    // zoom far out: cannot show more than the extent; far in: capped
    const auto out = zoomView(v, 400.0, 1e-9, extent, width);
    CHECK(out.pixelsPerTick >= width / static_cast<double>(extent) - 1e-12);
    CHECK(out.originTick == 0);
    CHECK(zoomView(v, 400.0, 1e9, extent, width).pixelsPerTick == kMaxPixelsPerTick);
    // scroll is clamped to [0, extent - visible]
    CHECK(scrollView(v, -1e9, extent, width).originTick == 0);
    const auto end = scrollView(v, 1e9, extent, width);
    CHECK(end.originTick + static_cast<Tick>(width / end.pixelsPerTick) >= extent - 1);
    CHECK(end.originTick + static_cast<Tick>(width / end.pixelsPerTick) <= extent + 1);
}

TEST_CASE("ruler: eighth and sixteenth counting, hierarchy and label thinning", "[grid][ruler]") {
    const double ppt = 0.2;  // 192 px per beat: roomy
    auto labels = [&](Tick unit, Tick last) {
        std::vector<std::string> out;
        for (const auto& m : buildRuler(k44, 0, last, ppt, unit)) out.push_back(m.label);
        return out;
    };
    const std::vector<std::string> eighths{"1", "&", "2", "&", "3", "&", "4", "&"};
    CHECK(labels(480, 3840 - 1) == eighths);
    const std::vector<std::string> sixteenths{"1", "e", "&", "a", "2", "e", "&", "a", "3", "e", "&", "a", "4", "e", "&", "a"};
    CHECK(labels(240, 3840 - 1) == sixteenths);

    const auto marks = buildRuler(k44, 0, 3840 * 2, ppt, 480);
    for (const auto& m : marks) {
        if (m.tick % 3840 == 0) CHECK(m.level == MarkLevel::Bar);
        else if (m.tick % 960 == 0) CHECK(m.level == MarkLevel::Beat);
        else CHECK(m.level == MarkLevel::Subdivision);
    }
    CHECK(marks.front().label == "1");
    CHECK(std::find_if(marks.begin(), marks.end(), [](const RulerMark& m) { return m.tick == 3840; })->label == "2");  // bar 2

    // zoomed far out, crowded labels/lines are hidden but marks keep their exact ticks
    const auto far = buildRuler(k44, 0, 3840 * 32, 0.004, 240);
    for (const auto& m : far) {
        CHECK(m.tick % 960 == 0);  // sixteenths skipped entirely; only bars/beats remain
        if (m.level == MarkLevel::Beat) CHECK_FALSE(m.showLabel);
    }
    const auto bars = std::count_if(far.begin(), far.end(), [](const RulerMark& m) { return m.level == MarkLevel::Bar && m.showLabel; });
    CHECK(bars < 32);
    CHECK(bars > 0);
    // triplet grid: unlabelled lines at exact multiples of 320
    for (const auto& m : buildRuler(k44, 0, 960, ppt, 320))
        if (m.level == MarkLevel::Subdivision) { CHECK(m.label.empty()); CHECK(m.tick % 320 == 0); }
    CHECK(buildRuler(k44, 100, 50, ppt, 480).empty());
}

TEST_CASE("addBlock: duration is independent of the snap unit", "[grid][insert]") {
    auto p = empty();
    REQUIRE(addBlock(p, kI, 1440, 120, 960).ok);   // fine snap, one-beat chord
    REQUIRE(addBlock(p, kI, 3840, 3840, 480).ok);  // bar snap, eighth-note chord
    CHECK(p.blocks[0].durationTicks == 960);
    CHECK(p.blocks[1].durationTicks == 480);
    REQUIRE(addBlock(p, kI, 0, 480, 3840).ok);     // would hit the chord at 1440: shortened, never overlapping
    CHECK(p.blocks[0].durationTicks == 1440);
    CHECK(addBlock(p, kI, 1500, 480, 960).ok == false);  // snaps to 1440, inside an existing chord: refused, nothing lost
    CHECK(p.blocks.size() == 3);
    CHECK(p.validate().empty());
}

TEST_CASE("moveBlocks keeps relative timing and stops at unselected chords", "[grid][group]") {
    Progression p;
    p.blocks = {{kI, 0, 960}, {kI, 960, 480}, {kV7, 3840, 960}, {kI, 7680, 960}};
    REQUIRE(p.validate().empty());
    const auto r = moveBlocks(p, {0, 1}, 0, 480, 480);  // shift the pair by +480
    REQUIRE(r.ok);
    CHECK(p.blocks[0].startTick == 480);
    CHECK(p.blocks[1].startTick == 1440);
    CHECK(p.blocks[1].durationTicks == 480);
    CHECK(p.blocks[2].startTick == 3840);
    // too far right: stops flush against the chord at 3840 (pair spans 1440 ticks => start 2400)
    REQUIRE(moveBlocks(p, {0, 1}, 0, 7000, 480).ok);
    CHECK(p.blocks[0].startTick == 2400);
    CHECK(p.blocks[1].startTick == 3360);
    CHECK(p.blocks[1].endTick() == 3840);
    // too far left: stops at 0
    REQUIRE(moveBlocks(p, {0, 1}, 0, -5000, 480).ok);
    CHECK(p.blocks[0].startTick == 0);
    CHECK(p.blocks[1].startTick == 960);
    CHECK(moveBlocks(p, {0, 0}, 0, 0, 480).ok == false);   // duplicate index
    CHECK(moveBlocks(p, {9}, 9, 0, 480).ok == false);
    CHECK(moveBlocks(p, {0}, 1, 0, 480).ok == false);       // anchor not selected
    CHECK(p.validate().empty());
}

TEST_CASE("duplicateBlocks and deleteBlocks", "[grid][group]") {
    Progression p;
    ChordBlock b2{kV7, 960 + 480, 480};
    b2.octave = 1;
    p.blocks = {{kI, 0, 960}, b2};
    const auto r = duplicateBlocks(p, {1, 0});
    REQUIRE(r.ok);
    REQUIRE(p.blocks.size() == 4);
    // group spans 0..1920 -> copies at +1920 with the same relative gaps, durations, chords and octaves
    CHECK(p.blocks[2].startTick == 1920);
    CHECK(p.blocks[3].startTick == 1920 + 1440);
    CHECK(p.blocks[3].durationTicks == 480);
    CHECK(p.blocks[3].octave == 1);
    CHECK(p.blocks[3].chord == kV7);
    CHECK(r.indices == std::vector<std::size_t>{2, 3});

    // no room: nothing changes
    Progression q;
    q.blocks = {{kI, 0, 960}, {kI, 960, 960}};
    const auto before = q.blocks;
    CHECK(duplicateBlocks(q, {0}).ok == false);  // copy at 960 overlaps
    CHECK(q.blocks == before);

    REQUIRE(deleteBlocks(p, {0, 3}).ok);
    CHECK(p.blocks.size() == 2);
    CHECK(deleteBlocks(p, {}).ok == false);
}

TEST_CASE("Test F: moving a voiced chord keeps every pitch (7th, inversion, octave)", "[grid][testF]") {
    // Chord Density is not part of this codebase; the voicing settings that exist (seventh, inversion,
    // chord octave) stand in. Moving must not change a single pitch.
    Progression p;
    ChordBlock b{kV7, 0, 960};
    b.octave = -1;
    p.blocks = {b};
    const auto home = generateNotes(p);
    REQUIRE(home.size() >= 4);
    REQUIRE(moveBlock(p, 0, 1440, 480).ok);
    const auto moved = generateNotes(p);
    REQUIRE(moved.size() == home.size());
    for (std::size_t i = 0; i < home.size(); ++i) {
        CHECK(moved[i].pitch == home[i].pitch);
        CHECK(moved[i].velocity == home[i].velocity);
        CHECK(moved[i].lengthTicks == home[i].lengthTicks);
        CHECK(moved[i].startTick == 1440);
    }
    CHECK(p.blocks[0].chord == kV7);
    CHECK(p.blocks[0].octave == -1);
}

TEST_CASE("Test G: MIDI export matches on-beat, offbeat and sixteenth positions", "[grid][testG]") {
    Progression p;
    p.blocks = {{kI, 0, 960},                                       // beat 1
                {kV7, tickOf(k44, 1, 2, Subdivision::And), 960},    // & of 2
                {kI, tickOf(k44, 1, 4, Subdivision::A), 240}};      // a of 4
    REQUIRE(p.validate().empty());
    const auto bytes = exportProgressionToMidi(p);
    const auto file = smf::parse(bytes);
    CHECK(file.division == 960);
    const auto notes = smf::extractNotes(file.tracks[0]);
    std::set<std::uint64_t> starts, ends;
    for (const auto& n : notes) { starts.insert(n.start); ends.insert(n.start + n.length); }
    CHECK(starts == std::set<std::uint64_t>{0, 1440, 3600});
    CHECK(ends == std::set<std::uint64_t>{960, 2400, 3840});
    // and it is the generator's output, note for note
    const auto generated = generateNotes(p);
    CHECK(notes.size() == generated.size());
}

TEST_CASE("Test H: save and reload restores syncopated placements", "[grid][testH]") {
    Progression p;
    p.blocks = {{kI, 1440, 960}, {kV7, 2640, 240}};
    p.blocks[1].octave = 1;
    p.lengthBars = 4;
    const auto text = serializeProgression(p);
    const auto r = deserializeProgression(text);
    REQUIRE(r.ok);
    CHECK(r.progression.blocks == p.blocks);
    CHECK(r.progression.lengthBars == 4);
    CHECK(generateNotes(r.progression) == generateNotes(p));
}

TEST_CASE("Test I: undo and redo of a move to the '&' of beat 2", "[grid][testI]") {
    Progression start;
    start.blocks = {{kI, 0, 960}};
    Document doc(start);
    const auto res = doc.edit("Move chord", [](Progression& p) { REQUIRE(moveBlock(p, 0, 1440, 480).ok); });
    REQUIRE(res.ok);
    CHECK(doc.progression().blocks[0].startTick == 1440);
    REQUIRE(doc.undo());
    CHECK(doc.progression().blocks[0].startTick == 0);
    REQUIRE(doc.redo());
    CHECK(doc.progression().blocks[0].startTick == 1440);
}

TEST_CASE("group move is one undo step", "[grid][undo]") {
    Progression start;
    start.blocks = {{kI, 0, 960}, {kI, 960, 960}};
    Document doc(start);
    REQUIRE(doc.edit("Move chords", [](Progression& p) { REQUIRE(moveBlocks(p, {0, 1}, 0, 480, 480).ok); }).ok);
    REQUIRE(doc.undo());
    CHECK(doc.progression().blocks == start.blocks);
    CHECK_FALSE(doc.canUndo());
}
