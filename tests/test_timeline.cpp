// Timeline editing operations, snapping and view geometry. Expected values are worked out by hand.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Document.h>
#include <chordcore/Progression.h>
#include <chordcore/Serialization.h>
#include <chordcore/Timeline.h>

#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>

using namespace chordcore;

namespace {

const ChordSpec kI{1, 0, Quality::Major, Seventh::None, 0, Suspension::None};
const ChordSpec kV7{5, 0, Quality::Major, Seventh::Minor, 0, Suspension::None};

std::vector<std::pair<Tick, Tick>> layout(const Progression& p) {
    std::vector<std::pair<Tick, Tick>> out;
    for (const auto& b : p.blocks) out.emplace_back(b.startTick, b.durationTicks);
    return out;
}
using Layout = std::vector<std::pair<Tick, Tick>>;

Progression withBlocks(std::initializer_list<std::pair<Tick, Tick>> spans) {
    Progression p;
    for (const auto& s : spans) p.blocks.push_back(ChordBlock{kI, s.first, s.second});
    REQUIRE(p.validate().empty());
    return p;
}

}  // namespace

// ---------------------------------------------------------------------------------------------- snapping

TEST_CASE("snapUnitTicks: grid sizes per time signature", "[timeline][snap]") {
    const TimeSignature t44{4, 4}, t34{3, 4}, t68{6, 8}, t22{2, 2};
    CHECK(snapUnitTicks(SnapMode::Off, t44) == 1);
    CHECK(snapUnitTicks(SnapMode::Bar, t44) == 3840);
    CHECK(snapUnitTicks(SnapMode::Beat, t44) == 960);
    CHECK(snapUnitTicks(SnapMode::HalfBeat, t44) == 480);
    CHECK(snapUnitTicks(SnapMode::Bar, t34) == 2880);
    CHECK(snapUnitTicks(SnapMode::Beat, t34) == 960);
    CHECK(snapUnitTicks(SnapMode::Bar, t68) == 2880);
    CHECK(snapUnitTicks(SnapMode::Beat, t68) == 480);
    CHECK(snapUnitTicks(SnapMode::HalfBeat, t68) == 240);
    CHECK(snapUnitTicks(SnapMode::Beat, t22) == 1920);
    CHECK(snapUnitTicks(SnapMode::Beat, TimeSignature{4, 3}) == 1);  // invalid signature: free
}

TEST_CASE("snapToUnit: nearest multiple, ties up, negatives handled", "[timeline][snap]") {
    CHECK(snapToUnit(0, 480) == 0);
    CHECK(snapToUnit(239, 480) == 0);
    CHECK(snapToUnit(240, 480) == 480);
    CHECK(snapToUnit(719, 480) == 480);
    CHECK(snapToUnit(720, 480) == 960);
    CHECK(snapToUnit(-239, 480) == 0);
    CHECK(snapToUnit(-240, 480) == 0);
    CHECK(snapToUnit(-241, 480) == -480);
    CHECK(snapToUnit(12345, 1) == 12345);
    CHECK(snapToUnit(12345, 0) == 12345);  // unit < 1 means free
    CHECK(snapToUnit(5, 960) == 0);
}

TEST_CASE("maxEndTick is 256 bars", "[timeline]") {
    CHECK(maxEndTick(TimeSignature{4, 4}) == 256 * 3840);
    CHECK(maxEndTick(TimeSignature{3, 4}) == 256 * 2880);
}

// ---------------------------------------------------------------------------------------------- move

TEST_CASE("moveBlock: snaps, clamps at neighbours, never jumps over them", "[timeline][move]") {
    auto p = makeDemoProgression();  // 0, 3840, 7680, 11520 each 3840 long

    SECTION("touching neighbours: nowhere to go") {
        REQUIRE(moveBlock(p, 1, 3840 + 5000, 960).ok);
        CHECK(p == makeDemoProgression());
        REQUIRE(moveBlock(p, 1, -5000, 960).ok);
        CHECK(p == makeDemoProgression());
    }
    SECTION("with a gap") {
        REQUIRE(deleteBlock(p, 2).ok);  // blocks at 0, 3840, 11520
        CHECK(moveBlock(p, 1, 8000, 960).ok);
        CHECK(p.blocks[1].startTick == 7680);  // 8000 snaps to 7680 (also the limit: 11520 - 3840)
        CHECK(moveBlock(p, 1, 9000, 960).ok);
        CHECK(p.blocks[1].startTick == 7680);  // snaps to 8640, clamped to 7680
        CHECK(moveBlock(p, 1, 5000, 960).ok);
        CHECK(p.blocks[1].startTick == 4800);  // 5000 -> 4800
        CHECK(p.blocks[1].durationTicks == 3840);
        CHECK(moveBlock(p, 1, -1000, 960).ok);
        CHECK(p.blocks[1].startTick == 3840);  // left limit: end of block 0
        CHECK(p.validate().empty());
    }
    SECTION("first block cannot go below zero; free grid is exact") {
        REQUIRE(deleteBlock(p, 1).ok);
        REQUIRE(moveBlock(p, 0, -777, 1).ok);
        CHECK(p.blocks[0].startTick == 0);
        REQUIRE(moveBlock(p, 0, 1234, 1).ok);
        CHECK(p.blocks[0].startTick == 1234);
        CHECK(p.blocks[0].endTick() == 1234 + 3840);
    }
    SECTION("last block can move right up to the limit") {
        REQUIRE(moveBlock(p, 3, 99'999'999, 3840).ok);
        CHECK(p.blocks[3].endTick() == maxEndTick(p.timeSignature));
    }
    SECTION("bad index") {
        const auto before = p;
        const auto r = moveBlock(p, 4, 0, 960);
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.error.empty());
        CHECK(p == before);
    }
}

// ---------------------------------------------------------------------------------------------- resize

TEST_CASE("resizeBlockEnd", "[timeline][resize]") {
    auto p = makeDemoProgression();

    SECTION("shorten, snapped") {
        REQUIRE(resizeBlockEnd(p, 0, 2000, 960).ok);
        CHECK(layout(p)[0] == std::make_pair(Tick{0}, Tick{1920}));
    }
    SECTION("cannot go below one grid unit") {
        REQUIRE(resizeBlockEnd(p, 0, 100, 960).ok);
        CHECK(p.blocks[0].durationTicks == 960);
        REQUIRE(resizeBlockEnd(p, 0, -5000, 960).ok);
        CHECK(p.blocks[0].durationTicks == 960);
    }
    SECTION("cannot grow into the next block") {
        REQUIRE(resizeBlockEnd(p, 0, 99999, 960).ok);
        CHECK(p.blocks[0].durationTicks == 3840);
    }
    SECTION("grows up to the next block across a gap") {
        REQUIRE(deleteBlock(p, 1).ok);  // 0, 7680, 11520
        REQUIRE(resizeBlockEnd(p, 0, 99999, 960).ok);
        CHECK(p.blocks[0].durationTicks == 7680);
        REQUIRE(resizeBlockEnd(p, 0, 5000, 960).ok);
        CHECK(p.blocks[0].durationTicks == 4800);
    }
    SECTION("the last block grows to the timeline limit") {
        REQUIRE(resizeBlockEnd(p, 3, 999'999'999, 3840).ok);
        CHECK(p.blocks[3].endTick() == maxEndTick(p.timeSignature));
    }
    SECTION("a block shorter than the grid is never forced to grow") {
        auto q = withBlocks({{0, 100}});
        REQUIRE(resizeBlockEnd(q, 0, 0, 960).ok);
        CHECK(q.blocks[0].durationTicks == 100);
    }
}

TEST_CASE("resizeBlockStart", "[timeline][resize]") {
    auto p = makeDemoProgression();

    SECTION("shorten from the left; the end stays") {
        REQUIRE(resizeBlockStart(p, 1, 5000, 960).ok);
        CHECK(p.blocks[1].startTick == 4800);
        CHECK(p.blocks[1].endTick() == 7680);
    }
    SECTION("cannot extend into the previous block") {
        REQUIRE(resizeBlockStart(p, 1, 100, 960).ok);
        CHECK(layout(p) == layout(makeDemoProgression()));
    }
    SECTION("extends across a gap, down to tick 0") {
        REQUIRE(deleteBlock(p, 0).ok);  // 3840.. ; gap before it
        REQUIRE(resizeBlockStart(p, 0, -5000, 960).ok);
        CHECK(p.blocks[0].startTick == 0);
        CHECK(p.blocks[0].endTick() == 7680);
    }
    SECTION("cannot shrink below one grid unit") {
        REQUIRE(resizeBlockStart(p, 1, 999'999, 960).ok);
        CHECK(p.blocks[1].durationTicks == 960);
        CHECK(p.blocks[1].endTick() == 7680);
    }
    SECTION("bad index") {
        CHECK_FALSE(resizeBlockStart(p, 9, 0, 960).ok);
        CHECK_FALSE(resizeBlockEnd(p, 9, 0, 960).ok);
    }
}

// ---------------------------------------------------------------------------------------------- add / split / duplicate / delete / set

TEST_CASE("addBlock", "[timeline][add]") {
    SECTION("into an empty progression: snapped, one bar long") {
        Progression p;
        const auto r = addBlock(p, kI, 5000, 3840);
        REQUIRE(r.ok);
        CHECK(r.index == 0);
        CHECK(layout(p) == Layout{{3840, 3840}});
    }
    SECTION("negative position lands on tick 0") {
        Progression p;
        REQUIRE(addBlock(p, kI, -9000, 960).ok);
        CHECK(p.blocks[0].startTick == 0);
    }
    SECTION("inside an existing chord is refused") {
        auto p = makeDemoProgression();
        const auto r = addBlock(p, kI, 100, 1);
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.error.empty());
        CHECK(p == makeDemoProgression());
        CHECK_FALSE(addBlock(p, kI, 3839, 1).ok);   // last tick of block 0
        CHECK(addBlock(p, kI, 15360, 1).ok);        // first tick after the last block
    }
    SECTION("fills a gap, shortened to fit, and keeps the order") {
        auto p = withBlocks({{0, 3840}, {4800, 3840}});
        const auto r = addBlock(p, kV7, 3840, 960);
        REQUIRE(r.ok);
        CHECK(r.index == 1);
        CHECK(layout(p) == Layout{{0, 3840}, {3840, 960}, {4800, 3840}});
        CHECK(p.blocks[1].chord == kV7);
        CHECK(p.validate().empty());
    }
    SECTION("a single free tick is enough on a free grid") {
        auto p = withBlocks({{0, 3840}, {3841, 100}});
        REQUIRE(addBlock(p, kI, 3840, 1).ok);
        CHECK(layout(p)[1] == std::make_pair(Tick{3840}, Tick{1}));
    }
    SECTION("inserts before later blocks") {
        auto p = withBlocks({{7680, 3840}});
        const auto r = addBlock(p, kI, 0, 3840);
        REQUIRE(r.ok);
        CHECK(r.index == 0);
        CHECK(layout(p) == Layout{{0, 3840}, {7680, 3840}});
    }
    SECTION("beyond the timeline limit; invalid chord") {
        Progression p;
        CHECK_FALSE(addBlock(p, kI, maxEndTick(p.timeSignature), 1).ok);
        CHECK(addBlock(p, kI, maxEndTick(p.timeSignature) - 3840, 1).ok);
        ChordSpec bad = kI;
        bad.degree = 9;
        CHECK_FALSE(addBlock(p, bad, 0, 1).ok);
    }
}

TEST_CASE("splitBlock", "[timeline][split]") {
    auto p = makeDemoProgression();

    SECTION("in the middle") {
        const auto r = splitBlock(p, 0, 1920, 960);
        REQUIRE(r.ok);
        CHECK(r.index == 1);
        REQUIRE(p.blocks.size() == 5);
        CHECK(layout(p)[0] == std::make_pair(Tick{0}, Tick{1920}));
        CHECK(layout(p)[1] == std::make_pair(Tick{1920}, Tick{1920}));
        CHECK(p.blocks[0].chord == p.blocks[1].chord);
        CHECK(layout(p)[2] == std::make_pair(Tick{3840}, Tick{3840}));
    }
    SECTION("snapped") {
        REQUIRE(splitBlock(p, 0, 1000, 960).ok);
        CHECK(p.blocks[0].durationTicks == 960);
        CHECK(p.blocks[1].durationTicks == 2880);
    }
    SECTION("must be strictly inside") {
        CHECK_FALSE(splitBlock(p, 0, 0, 960).ok);
        CHECK_FALSE(splitBlock(p, 0, 100, 960).ok);    // snaps to 0
        CHECK_FALSE(splitBlock(p, 0, 3840, 960).ok);
        CHECK_FALSE(splitBlock(p, 0, 3700, 960).ok);   // snaps to 3840
        CHECK_FALSE(splitBlock(p, 9, 100, 1).ok);
        CHECK(p == makeDemoProgression());
    }
    SECTION("the notes are unchanged by a split") {
        // Same chord on two touching blocks: the progression stays valid and plays the same chords.
        REQUIRE(splitBlock(p, 2, 9600, 960).ok);
        CHECK(p.validate().empty());
    }
}

TEST_CASE("duplicateBlock", "[timeline][duplicate]") {
    auto p = makeDemoProgression();
    SECTION("no room before the next block") {
        CHECK_FALSE(duplicateBlock(p, 0).ok);
        CHECK(p == makeDemoProgression());
    }
    SECTION("after the last block") {
        const auto r = duplicateBlock(p, 3);
        REQUIRE(r.ok);
        CHECK(r.index == 4);
        CHECK(layout(p)[4] == std::make_pair(Tick{15360}, Tick{3840}));
        CHECK(p.blocks[4].chord == p.blocks[3].chord);
    }
    SECTION("into a gap that is exactly big enough, but not one tick smaller") {
        auto q = withBlocks({{0, 960}, {1920, 960}});
        REQUIRE(duplicateBlock(q, 0).ok);
        CHECK(layout(q) == Layout{{0, 960}, {960, 960}, {1920, 960}});
        auto r = withBlocks({{0, 960}, {1919, 960}});
        CHECK_FALSE(duplicateBlock(r, 0).ok);
    }
    SECTION("not past the timeline limit") {
        auto q = withBlocks({{maxEndTick(TimeSignature{}) - 960, 960}});
        CHECK_FALSE(duplicateBlock(q, 0).ok);
    }
}

TEST_CASE("deleteBlock and setBlockChord", "[timeline]") {
    auto p = makeDemoProgression();
    REQUIRE(deleteBlock(p, 1).ok);
    CHECK(layout(p) == Layout{{0, 3840}, {7680, 3840}, {11520, 3840}});
    CHECK_FALSE(deleteBlock(p, 3).ok);

    REQUIRE(setBlockChord(p, 0, kV7).ok);
    CHECK(p.blocks[0].chord == kV7);
    CHECK(p.blocks[0].startTick == 0);
    CHECK(p.blocks[0].durationTicks == 3840);
    ChordSpec bad = kI;
    bad.inversion = 3;  // a triad has no third inversion
    CHECK_FALSE(setBlockChord(p, 0, bad).ok);
    CHECK(p.blocks[0].chord == kV7);
    CHECK_FALSE(setBlockChord(p, 7, kI).ok);
}

// ---------------------------------------------------------------------------------------------- view and hit testing

TEST_CASE("TimelineView: pixels <-> ticks", "[timeline][view]") {
    TimelineView v;
    v.pixelsPerTick = 0.1;
    CHECK(v.tickToX(960) == 96.0);
    CHECK(v.xToTick(96.0) == 960);
    CHECK(v.xToTick(96.04) == 960);
    CHECK(v.xToTick(96.5) == 965);
    CHECK(v.xToTick(-10.0) == -100);

    v.originTick = 480;
    CHECK(v.tickToX(480) == 0.0);
    CHECK(v.tickToX(0) == -48.0);
    CHECK(v.xToTick(0.0) == 480);
    CHECK(v.xToTick(96.0) == 1440);

    v.pixelsPerTick = 0.0;  // degenerate zoom must not divide by zero
    CHECK(v.xToTick(50.0) == 480);
    v.pixelsPerTick = 0.1;
    CHECK(v.xToTick(std::numeric_limits<double>::quiet_NaN()) == 480);
}

TEST_CASE("fitPixelsPerTick", "[timeline][view]") {
    CHECK(fitPixelsPerTick(makeDemoProgression(), 960.0) == 960.0 / 15360.0);  // four bars fill the width
    CHECK(fitPixelsPerTick(Progression{}, 960.0) == 960.0 / 15360.0);          // at least four bars
    auto p = makeDemoProgression();
    p.blocks.push_back(ChordBlock{kI, 15360, 3840 * 4});                        // 8 bars
    CHECK(fitPixelsPerTick(p, 960.0) == 960.0 / (3840.0 * 8));
    p.blocks.push_back(ChordBlock{kI, 30720, 1});                               // one tick into bar 9: 9 bars
    CHECK(fitPixelsPerTick(p, 960.0) == 960.0 / (3840.0 * 9));
    CHECK(fitPixelsPerTick(p, 0.0) > 0.0);
}

TEST_CASE("hitTest: block body and edges", "[timeline][hit]") {
    TimelineView v;
    v.pixelsPerTick = 0.1;  // block 0 spans x 0..384, block 1 384..768
    const auto p = makeDemoProgression();

    auto hit = [&](double x) { return hitTest(p, v, x, 6.0); };
    CHECK(hit(-1).index == -1);
    CHECK(hit(-1).zone == HitZone::None);
    CHECK(hit(0).index == 0);
    CHECK(hit(0).zone == HitZone::LeftEdge);
    CHECK(hit(5.9).zone == HitZone::LeftEdge);
    CHECK(hit(6.0).zone == HitZone::Body);
    CHECK(hit(190).zone == HitZone::Body);
    CHECK(hit(377.9).zone == HitZone::Body);
    CHECK(hit(378).zone == HitZone::RightEdge);
    CHECK(hit(383.9).index == 0);
    CHECK(hit(384).index == 1);          // the boundary belongs to the next block
    CHECK(hit(384).zone == HitZone::LeftEdge);
    CHECK(hit(1535.9).index == 3);
    CHECK(hit(1536).index == -1);

    SECTION("a narrow block keeps a body") {
        auto q = withBlocks({{0, 30}});  // 3 px wide: edges shrink to 1 px each
        CHECK(hitTest(q, v, 0.5, 6.0).zone == HitZone::LeftEdge);
        CHECK(hitTest(q, v, 1.5, 6.0).zone == HitZone::Body);
        CHECK(hitTest(q, v, 2.5, 6.0).zone == HitZone::RightEdge);
    }
    SECTION("scrolled view") {
        v.originTick = 3840;
        CHECK(hitTest(p, v, 1.0, 6.0).index == 1);
        CHECK(hitTest(p, v, -1.0, 6.0).index == 0);
    }
}

// ---------------------------------------------------------------------------------------------- properties

TEST_CASE("property: random edit sequences keep the progression valid, sorted, and on the grid", "[timeline][property]") {
    std::mt19937 rng(61);
    const SnapMode modes[] = {SnapMode::Bar, SnapMode::Beat, SnapMode::HalfBeat};
    const TimeSignature signatures[] = {{4, 4}, {3, 4}, {6, 8}, {5, 4}, {7, 8}};

    for (int trial = 0; trial < 200; ++trial) {
        Progression p;
        p.timeSignature = signatures[rng() % 5];
        const Tick unit = snapUnitTicks(modes[rng() % 3], p.timeSignature);
        const Tick span = unit * 40;
        auto randomTick = [&] { return static_cast<Tick>(rng() % static_cast<unsigned>(span * 2)) - span / 4; };
        auto randomChord = [&] {
            ChordSpec c{1 + static_cast<int>(rng() % 7), 0, Quality::Major, Seventh::None, 0, Suspension::None};
            return c;
        };

        for (int step = 0; step < 60; ++step) {
            const auto before = p;
            const std::size_t index = p.blocks.empty() ? 0 : rng() % (p.blocks.size() + 1);  // sometimes out of range
            OpResult r;
            switch (rng() % 8) {
                case 0: r = addBlock(p, randomChord(), randomTick(), unit); break;
                case 1: r = moveBlock(p, index, randomTick(), unit); break;
                case 2: r = resizeBlockStart(p, index, randomTick(), unit); break;
                case 3: r = resizeBlockEnd(p, index, randomTick(), unit); break;
                case 4: r = splitBlock(p, index, randomTick(), unit); break;
                case 5: r = duplicateBlock(p, index); break;
                case 6: r = deleteBlock(p, index); break;
                default: r = setBlockChord(p, index, randomChord()); break;
            }
            INFO("trial " << trial << " step " << step);
            if (!r.ok) {
                REQUIRE_FALSE(r.error.empty());
                REQUIRE(p == before);  // a refused operation changes nothing
                continue;
            }
            REQUIRE(p.validate().empty());
            for (std::size_t i = 0; i < p.blocks.size(); ++i) {
                REQUIRE(p.blocks[i].startTick % unit == 0);
                REQUIRE(p.blocks[i].durationTicks % unit == 0);
                REQUIRE(p.blocks[i].endTick() <= maxEndTick(p.timeSignature));
                if (i > 0) REQUIRE(p.blocks[i - 1].endTick() <= p.blocks[i].startTick);  // sorted, no overlap
            }
        }

        // Whatever the editor produced saves and loads back identically.
        const auto reloaded = deserializeProgression(serializeProgression(p));
        REQUIRE(reloaded.ok);
        REQUIRE(reloaded.progression == p);
    }
}

TEST_CASE("property: move keeps length and chord; resize keeps the opposite edge", "[timeline][property]") {
    std::mt19937 rng(8);
    for (int trial = 0; trial < 300; ++trial) {
        auto p = makeDemoProgression();
        deleteBlock(p, rng() % 4);
        const Tick unit = (rng() % 2) ? 960 : 1;
        const std::size_t i = rng() % p.blocks.size();
        const auto before = p.blocks[i];
        const Tick target = static_cast<Tick>(rng() % 30000) - 5000;

        auto moved = p;
        REQUIRE(moveBlock(moved, i, target, unit).ok);
        CHECK(moved.blocks[i].durationTicks == before.durationTicks);
        CHECK(moved.blocks[i].chord == before.chord);
        CHECK(moved.validate().empty());

        auto left = p;
        REQUIRE(resizeBlockStart(left, i, target, unit).ok);
        CHECK(left.blocks[i].endTick() == before.endTick());
        CHECK(left.validate().empty());

        auto right = p;
        REQUIRE(resizeBlockEnd(right, i, target, unit).ok);
        CHECK(right.blocks[i].startTick == before.startTick);
        CHECK(right.validate().empty());
    }
}

// ---------------------------------------------------------------------------------------------- with the Document

TEST_CASE("timeline operations through Document: undoable, and a failed one is rejected cleanly", "[timeline][document]") {
    Document d;
    const auto original = d.progression();

    auto run = [&d](const char* label, std::function<OpResult(Progression&)> op) {
        return d.edit(label, [&op](Progression& p) {
            const auto r = op(p);
            if (!r.ok) throw std::runtime_error(r.error);
        });
    };

    REQUIRE(run("Resize", [](Progression& p) { return resizeBlockEnd(p, 0, 1920, 960); }).ok);
    REQUIRE(run("Add chord", [](Progression& p) { return addBlock(p, kV7, 1920, 960); }).ok);
    CHECK(d.progression().blocks.size() == 5);

    const auto beforeFailure = d.progression();
    const auto bad = run("Add chord", [](Progression& p) { return addBlock(p, kI, 0, 960); });  // occupied
    CHECK_FALSE(bad.ok);
    CHECK(bad.error.find("already") != std::string::npos);
    CHECK(d.progression() == beforeFailure);

    REQUIRE(d.undo());
    REQUIRE(d.undo());
    CHECK(d.progression() == original);
}
