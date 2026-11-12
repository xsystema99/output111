// AuditionPlayer: one-shot chord for the palette. The invariant is "every note-on gets exactly one note-off".

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Audition.h>

#include <map>
#include <random>

using namespace chordcore;

namespace {
/// Replays events and returns the set of pitches currently sounding; fails the test on a double on/off.
struct Tracker {
    std::map<std::pair<int, int>, int> on;  // (channel, pitch) -> count
    void apply(const std::vector<ScheduledEvent>& events) {
        for (const auto& e : events) {
            auto& n = on[{e.channel, e.pitch}];
            if (e.noteOn) { CHECK(n == 0); ++n; }
            else { CHECK(n == 1); --n; }
        }
    }
    int sounding() const { int total = 0; for (const auto& [k, v] : on) total += v; return total; }
};
}  // namespace

TEST_CASE("audition: notes start at offset 0 and release after the hold time to the sample", "[audition]") {
    AuditionPlayer a(0.5);  // 500 samples at 1000 Hz
    a.request({60, 64, 67}, 100, 2);
    auto first = a.process(100, 1000.0);
    REQUIRE(first.size() == 3);
    for (const auto& e : first) { CHECK(e.noteOn); CHECK(e.sampleOffset == 0); CHECK(e.velocity == 100); CHECK(e.channel == 2); }

    // blocks of 100: still sounding through the 4th block (samples 100..399), released in the 5th at offset 0 (sample 500 total=block 6)
    int total = 100;
    std::vector<ScheduledEvent> offs;
    while (offs.empty() && total < 2000) {
        auto ev = a.process(100, 1000.0);
        if (!ev.empty()) { offs = ev; break; }
        total += 100;
    }
    REQUIRE(offs.size() == 3);
    for (const auto& e : offs) { CHECK_FALSE(e.noteOn); CHECK(e.velocity == 0); CHECK(total + e.sampleOffset == 500); }
    CHECK_FALSE(a.sounding());
}

TEST_CASE("audition: a new request releases the old chord first, in the same block", "[audition]") {
    AuditionPlayer a(1.0);
    Tracker t;
    a.request({60, 64, 67}, 96, 0);
    t.apply(a.process(64, 48000.0));
    a.request({62, 65, 69}, 96, 0);
    const auto ev = a.process(64, 48000.0);
    t.apply(ev);
    REQUIRE(ev.size() == 6);
    for (int i = 0; i < 3; ++i) CHECK_FALSE(ev[static_cast<std::size_t>(i)].noteOn);  // old notes off first
    for (int i = 3; i < 6; ++i) CHECK(ev[static_cast<std::size_t>(i)].noteOn);
    CHECK(t.sounding() == 3);
}

TEST_CASE("audition: same pitches requested again are re-struck, never doubled", "[audition]") {
    AuditionPlayer a(1.0);
    Tracker t;
    a.request({60, 60, 64}, 96, 0);  // duplicate pitch in the request
    t.apply(a.process(64, 48000.0));
    CHECK(t.sounding() == 2);
    a.request({60, 64}, 96, 0);
    t.apply(a.process(64, 48000.0));
    CHECK(t.sounding() == 2);
}

TEST_CASE("audition: stopAll releases at once and drops a pending request", "[audition]") {
    AuditionPlayer a(1.0);
    Tracker t;
    a.request({60, 64}, 96, 0);
    t.apply(a.process(64, 48000.0));
    a.request({70}, 96, 0);
    a.stopAll();
    t.apply(a.process(64, 48000.0));
    CHECK(t.sounding() == 0);
    CHECK_FALSE(a.sounding());
    t.apply(a.process(64, 48000.0));
    CHECK(t.sounding() == 0);
}

TEST_CASE("audition: out-of-range pitches are skipped, empty request does nothing", "[audition]") {
    AuditionPlayer a;
    a.request({}, 96, 0);
    CHECK(a.process(64, 48000.0).empty());
    a.request({-1, 128, 60}, 96, 0);
    const auto ev = a.process(64, 48000.0);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].pitch == 60);
}

TEST_CASE("audition: random requests and block sizes never leave a note sounding", "[audition][property]") {
    std::mt19937 rng(7);
    for (int round = 0; round < 200; ++round) {
        AuditionPlayer a(0.05 + 0.01 * static_cast<double>(rng() % 30));
        Tracker t;
        for (int step = 0; step < 60; ++step) {
            const auto roll = rng() % 10;
            if (roll < 2) a.request({60 + static_cast<int>(rng() % 12), 64 + static_cast<int>(rng() % 12), 67}, 96, static_cast<int>(rng() % 2));
            else if (roll == 2) a.stopAll();
            t.apply(a.process(1 + static_cast<int>(rng() % 700), 48000.0));
        }
        a.stopAll();
        t.apply(a.process(512, 48000.0));
        CHECK(t.sounding() == 0);
    }
}

TEST_CASE("audition: a release never precedes its note-on, even with one-sample blocks", "[audition]") {
    AuditionPlayer a(0.00001);
    Tracker t;
    a.request({60}, 96, 0);
    auto ev = a.process(1, 48000.0);
    t.apply(ev);
    REQUIRE(ev.size() == 1);
    CHECK(ev[0].noteOn);
    ev = a.process(1, 48000.0);  // the release arrives in the next block, at its first sample
    t.apply(ev);
    REQUIRE(ev.size() == 1);
    CHECK_FALSE(ev[0].noteOn);
    CHECK(ev[0].sampleOffset == 0);
    CHECK(t.sounding() == 0);
}

TEST_CASE("audition: process() reuses its buffer (no allocation on the audio thread)", "[audition][realtime]") {
    AuditionPlayer a(0.05);
    std::vector<int> all;
    for (int p = 0; p < 128; ++p) all.push_back(p);
    a.request(all, 90, 0);
    const auto* first = &a.process(64, 48000.0);
    const auto* data = first->data();
    CHECK(first->size() == 128);
    a.request(all, 90, 1);  // the biggest case: 128 releases then 128 note-ons
    const auto& second = a.process(64, 48000.0);
    CHECK(&second == first);
    CHECK(second.data() == data);  // same storage: nothing was reallocated
    CHECK(second.size() == 256);
    for (int i = 0; i < 20; ++i) a.process(64, 48000.0);
    CHECK(a.process(64, 48000.0).data() == data);
}
