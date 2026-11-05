// PreviewTransport: the internal clock behind the Preview button, alone and driving the real scheduler.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/NoteEngine.h>
#include <chordcore/Preview.h>
#include <chordcore/Progression.h>
#include <chordcore/Scheduler.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <vector>

using namespace chordcore;

namespace {

constexpr double kSr = 48000.0;
constexpr double kBpm = 120.0;
constexpr int kBlock = 480;                       // 0.02 quarter notes per block at 120 BPM
constexpr double kEnd = 16.0;                     // the demo: four 4/4 bars

PreviewTransport::Result stepOnce(PreviewTransport& t, bool requested, bool hostPlaying = false, double endPpq = kEnd) {
    return t.step(requested, hostPlaying, kBpm, kSr, kBlock, endPpq);
}

long long expectedSample(Tick tick, double bpm, double sr) {
    return std::llround(static_cast<double>(tick) * sr * 60.0 / (bpm * kTicksPerQuarter));
}

using Entry = std::tuple<long long, int, int>;  // sample, 0 = off / 1 = on, pitch

}  // namespace

TEST_CASE("preview: idle when nothing is requested", "[preview]") {
    PreviewTransport t;
    for (int i = 0; i < 5; ++i) {
        const auto r = stepOnce(t, false);
        CHECK_FALSE(r.overrides);
        CHECK_FALSE(r.requestStillWanted);
    }
    CHECK_FALSE(t.running());
}

TEST_CASE("preview: starts at position 0 and advances by exactly the block length", "[preview]") {
    PreviewTransport t;
    for (int i = 0; i < 10; ++i) {
        const auto r = stepOnce(t, true);
        REQUIRE(r.overrides);
        CHECK(r.requestStillWanted);
        CHECK(r.block.playing);
        CHECK(r.block.numSamples == kBlock);
        CHECK(r.block.bpm == kBpm);
        CHECK(r.block.sampleRate == kSr);
        CHECK(std::abs(r.block.ppqStart - i * 0.02) < 1e-9);
    }
    CHECK(t.running());
}

TEST_CASE("preview: follows tempo changes between blocks", "[preview]") {
    PreviewTransport t;
    const auto first = t.step(true, false, 120.0, kSr, kBlock, kEnd);
    const auto second = t.step(true, false, 60.0, kSr, kBlock, kEnd);
    const auto third = t.step(true, false, 60.0, kSr, kBlock, kEnd);
    CHECK(std::abs(second.block.ppqStart - 0.02) < 1e-12);            // advanced at the old tempo
    CHECK(std::abs(third.block.ppqStart - (0.02 + 0.01)) < 1e-12);    // then at 60 BPM
    CHECK(first.block.ppqStart == 0.0);
}

TEST_CASE("preview: ends by itself with one stopped block, then reports finished", "[preview]") {
    PreviewTransport t;
    int playingBlocks = 0;
    PreviewTransport::Result last;
    for (int i = 0; i < 2000; ++i) {
        last = stepOnce(t, true);
        if (!last.requestStillWanted) break;
        ++playingBlocks;
    }
    CHECK(playingBlocks >= 799);
    CHECK(playingBlocks <= 801);  // 16 quarter notes / 0.02 per block
    REQUIRE(last.overrides);
    CHECK_FALSE(last.block.playing);  // the stopped block that releases notes
    CHECK_FALSE(t.running());

    // The caller clears its flag; afterwards nothing is produced.
    const auto after = stepOnce(t, false);
    CHECK_FALSE(after.overrides);
}

TEST_CASE("preview: withdrawing the request stops it with one stopped block", "[preview]") {
    PreviewTransport t;
    for (int i = 0; i < 20; ++i) stepOnce(t, true);
    const auto stop = stepOnce(t, false);
    REQUIRE(stop.overrides);
    CHECK_FALSE(stop.block.playing);
    CHECK_FALSE(stop.requestStillWanted);
    const auto idle = stepOnce(t, false);
    CHECK_FALSE(idle.overrides);

    // A new request starts again from the beginning.
    const auto again = stepOnce(t, true);
    CHECK(again.block.ppqStart == 0.0);
}

TEST_CASE("preview: the host wins", "[preview]") {
    PreviewTransport t;
    SECTION("a request is ignored while the host plays") {
        const auto r = stepOnce(t, true, true);
        CHECK_FALSE(r.overrides);
        CHECK_FALSE(r.requestStillWanted);  // the caller clears the request
        CHECK_FALSE(t.running());
    }
    SECTION("a running preview is cancelled when the host starts") {
        for (int i = 0; i < 10; ++i) stepOnce(t, true);
        const auto r = stepOnce(t, true, true);
        CHECK_FALSE(r.overrides);  // the host's own block now drives the scheduler
        CHECK_FALSE(r.requestStillWanted);
        CHECK_FALSE(t.running());
        // When the host stops again nothing stray is produced.
        const auto after = stepOnce(t, false, false);
        CHECK_FALSE(after.overrides);
    }
}

TEST_CASE("preview: nothing to play, or a bad block, finishes the request", "[preview]") {
    PreviewTransport t;
    CHECK_FALSE(stepOnce(t, true, false, 0.0).requestStillWanted);       // empty progression
    CHECK_FALSE(t.step(true, false, kBpm, kSr, 0, kEnd).requestStillWanted);
    CHECK_FALSE(t.step(true, false, 0.0, kSr, kBlock, kEnd).requestStillWanted);
    CHECK_FALSE(t.running());
}

TEST_CASE("preview: reset forgets a running preview", "[preview]") {
    PreviewTransport t;
    for (int i = 0; i < 5; ++i) stepOnce(t, true);
    t.reset();
    CHECK_FALSE(t.running());
    CHECK(stepOnce(t, true).block.ppqStart == 0.0);
}

TEST_CASE("preview through the scheduler: exactly the notes of the progression, on exact samples", "[preview][scheduler]") {
    const auto progression = makeDemoProgression();
    const auto notes = generateNotes(progression);
    PlaybackScheduler scheduler;
    scheduler.setNotes(notes);
    PreviewTransport preview;

    std::vector<Entry> log;
    long long sample = 0;
    for (int i = 0; i < 2000; ++i, sample += kBlock) {
        const auto r = stepOnce(preview, true);
        if (!r.overrides) break;
        for (const auto& e : scheduler.process(r.block)) log.emplace_back(sample + e.sampleOffset, e.noteOn ? 1 : 0, e.pitch);
        if (!r.requestStillWanted) break;
    }

    std::vector<Entry> expected;
    for (const auto& n : notes) {
        expected.emplace_back(expectedSample(n.startTick, kBpm, kSr), 1, n.pitch);
        expected.emplace_back(expectedSample(n.startTick + n.lengthTicks, kBpm, kSr), 0, n.pitch);
    }
    std::sort(log.begin(), log.end());
    std::sort(expected.begin(), expected.end());
    CHECK(log == expected);
    CHECK(scheduler.activeNoteCount() == 0);
}

TEST_CASE("preview through the scheduler: stopping mid-chord releases every note", "[preview][scheduler]") {
    PlaybackScheduler scheduler;
    scheduler.setNotes(generateNotes(makeDemoProgression()));
    PreviewTransport preview;

    std::map<int, int> sounding;  // pitch -> balance
    auto run = [&](bool requested) {
        const auto r = stepOnce(preview, requested);
        REQUIRE(r.overrides);
        for (const auto& e : scheduler.process(r.block)) sounding[e.pitch] += e.noteOn ? 1 : -1;
        return r;
    };
    for (int i = 0; i < 100; ++i) run(true);  // two quarter notes in: bar 1 chord is sounding
    CHECK(scheduler.activeNoteCount() > 0);
    run(false);                                // withdrawn: stopped block
    CHECK(scheduler.activeNoteCount() == 0);
    for (const auto& entry : sounding) CHECK(entry.second == 0);
}

TEST_CASE("preview through the scheduler: editing the chords while previewing leaves no stuck note", "[preview][scheduler]") {
    auto progression = makeDemoProgression();
    PlaybackScheduler scheduler;
    scheduler.setNotes(generateNotes(progression));
    PreviewTransport preview;

    std::map<std::pair<int, int>, int> sounding;  // (channel, pitch) -> balance
    for (int i = 0; i < 3000; ++i) {
        if (i == 150) {  // mid bar 1: the user changes the key
            progression.key.tonic = 2;
            scheduler.setNotes(generateNotes(progression));
        }
        if (i == 450) {  // and again, mid bar 2
            progression.key.tonic = 7;
            scheduler.setNotes(generateNotes(progression));
        }
        const auto r = stepOnce(preview, true);
        if (!r.overrides) break;
        for (const auto& e : scheduler.process(r.block)) sounding[{e.channel, e.pitch}] += e.noteOn ? 1 : -1;
        if (!r.requestStillWanted) break;
    }
    for (const auto& entry : sounding) CHECK(entry.second == 0);
    CHECK(scheduler.activeNoteCount() == 0);
}
