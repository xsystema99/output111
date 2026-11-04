// Progression model: time arithmetic and validation.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Progression.h>
#include <chordcore/Types.h>

#include <cmath>
#include <limits>

using namespace chordcore;

namespace {
const ChordSpec kTonic{1, 0, Quality::Major, Seventh::None, 0};
const ChordSpec kDominant{5, 0, Quality::Major, Seventh::None, 0};

Progression twoBlocks(Tick start0, Tick dur0, Tick start1, Tick dur1) {
    Progression p;
    p.blocks = {ChordBlock{kTonic, start0, dur0}, ChordBlock{kDominant, start1, dur1}};
    return p;
}
}  // namespace

TEST_CASE("kTicksPerQuarter is 960", "[progression][time]") { CHECK(kTicksPerQuarter == 960); }

TEST_CASE("TimeSignature::ticksPerBar", "[progression][time]") {
    CHECK(TimeSignature{4, 4}.ticksPerBar() == 3840);
    CHECK(TimeSignature{3, 4}.ticksPerBar() == 2880);
    CHECK(TimeSignature{6, 8}.ticksPerBar() == 2880);
    CHECK(TimeSignature{2, 2}.ticksPerBar() == 3840);
    CHECK(TimeSignature{5, 4}.ticksPerBar() == 4800);
    CHECK(TimeSignature{7, 8}.ticksPerBar() == 3360);
    CHECK(TimeSignature{12, 8}.ticksPerBar() == 5760);
    CHECK(TimeSignature{1, 16}.ticksPerBar() == 240);
    CHECK(TimeSignature{1, 64}.ticksPerBar() == 60);
}

TEST_CASE("TimeSignature::isValid", "[progression][time]") {
    CHECK(TimeSignature{4, 4}.isValid());
    CHECK(TimeSignature{7, 8}.isValid());
    CHECK(TimeSignature{1, 1}.isValid());
    CHECK(TimeSignature{32, 64}.isValid());
    CHECK_FALSE(TimeSignature{0, 4}.isValid());
    CHECK_FALSE(TimeSignature{33, 4}.isValid());
    CHECK_FALSE(TimeSignature{4, 0}.isValid());
    CHECK_FALSE(TimeSignature{4, 3}.isValid());
    CHECK_FALSE(TimeSignature{4, 128}.isValid());
    CHECK_FALSE(TimeSignature{-4, 4}.isValid());
}

TEST_CASE("Progression: endTick and lengthTicks round up to whole bars", "[progression][time]") {
    Progression empty;
    CHECK(empty.endTick() == 0);
    CHECK(empty.lengthTicks() == 0);

    CHECK(twoBlocks(0, 3840, 3840, 3840).endTick() == 7680);
    CHECK(twoBlocks(0, 3840, 3840, 3840).lengthTicks() == 7680);

    CHECK(twoBlocks(0, 1920, 1920, 960).endTick() == 2880);
    CHECK(twoBlocks(0, 1920, 1920, 960).lengthTicks() == 3840);  // padded to a whole 4/4 bar

    auto p = twoBlocks(0, 2880, 2880, 100);
    p.timeSignature = TimeSignature{3, 4};
    CHECK(p.lengthTicks() == 5760);  // 2880 + 100 -> 2 bars of 2880

    // endTick uses the furthest end even if blocks are not stored in time order
    CHECK(twoBlocks(3840, 3840, 0, 3840).endTick() == 7680);
}

TEST_CASE("Progression::validate: demo progression is valid", "[progression]") {
    const auto p = makeDemoProgression();
    CHECK(p.validate().empty());
    CHECK(p.blocks.size() == 4);
    CHECK(p.key == Key{0, Mode::Major});
    CHECK(p.tempoBpm == 120.0);
    CHECK(p.timeSignature == TimeSignature{4, 4});
    CHECK(p.lengthTicks() == 15360);
}

TEST_CASE("Progression::validate: demo is exactly I - V - vi - IV, one bar each", "[progression]") {
    const auto p = makeDemoProgression();
    REQUIRE(p.blocks.size() == 4);
    const int degrees[4] = {1, 5, 6, 4};
    const Quality qualities[4] = {Quality::Major, Quality::Major, Quality::Minor, Quality::Major};
    for (int i = 0; i < 4; ++i) {
        CHECK(p.blocks[i].chord.degree == degrees[i]);
        CHECK(p.blocks[i].chord.quality == qualities[i]);
        CHECK(p.blocks[i].chord.seventh == Seventh::None);
        CHECK(p.blocks[i].chord.inversion == 0);
        CHECK(p.blocks[i].startTick == 3840 * i);
        CHECK(p.blocks[i].durationTicks == 3840);
    }
}

TEST_CASE("Progression::validate: detects problems", "[progression]") {
    SECTION("adjacent blocks are fine") { CHECK(twoBlocks(0, 3840, 3840, 3840).validate().empty()); }
    SECTION("gaps (rests) are fine") { CHECK(twoBlocks(0, 960, 3840, 960).validate().empty()); }
    SECTION("overlap") { CHECK_FALSE(twoBlocks(0, 3840, 3839, 3840).validate().empty()); }
    SECTION("overlap regardless of storage order") { CHECK_FALSE(twoBlocks(3000, 3840, 0, 3840).validate().empty()); }
    SECTION("zero duration") { CHECK_FALSE(twoBlocks(0, 0, 3840, 3840).validate().empty()); }
    SECTION("negative duration") { CHECK_FALSE(twoBlocks(0, -5, 3840, 3840).validate().empty()); }
    SECTION("negative start") { CHECK_FALSE(twoBlocks(-1, 3840, 3840, 3840).validate().empty()); }
    SECTION("invalid chord") {
        auto p = twoBlocks(0, 3840, 3840, 3840);
        p.blocks[0].chord.degree = 9;
        CHECK_FALSE(p.validate().empty());
    }
    SECTION("tempo out of range / not finite") {
        auto p = makeDemoProgression();
        for (double bpm : {0.0, 19.9, 999.1, -120.0, std::numeric_limits<double>::quiet_NaN(),
                           std::numeric_limits<double>::infinity()}) {
            p.tempoBpm = bpm;
            INFO("tempo " << bpm);
            CHECK_FALSE(p.validate().empty());
        }
        for (double bpm : {20.0, 120.0, 999.0}) {
            p.tempoBpm = bpm;
            CHECK(p.validate().empty());
        }
    }
    SECTION("invalid time signature") {
        auto p = makeDemoProgression();
        p.timeSignature = TimeSignature{4, 3};
        CHECK_FALSE(p.validate().empty());
    }
    SECTION("invalid key") {
        auto p = makeDemoProgression();
        p.key.tonic = 12;
        CHECK_FALSE(p.validate().empty());
    }
    SECTION("empty progression is valid") { CHECK(Progression{}.validate().empty()); }
}
