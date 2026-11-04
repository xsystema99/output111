// PlaybackScheduler: sample-accurate placement, stop/locate/loop behaviour, stuck-note protection,
// and agreement with the exported MIDI file. All transports are synthetic.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>
#include <chordcore/Scheduler.h>

#include "support/SmfParser.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <tuple>
#include <vector>

using namespace chordcore;

namespace {

struct Logged {
    long long sample;  // absolute sample
    bool on;
    int pitch;
    int velocity;
    int channel;
    bool operator==(const Logged& o) const {
        return sample == o.sample && on == o.on && pitch == o.pitch && velocity == o.velocity && channel == o.channel;
    }
};

/// Absolute sample at which `tick` sounds, from first principles.
long long expectedSample(double tick, double bpm, double sr) {
    return std::llround(tick * sr * 60.0 / (bpm * kTicksPerQuarter));
}

struct Rig {
    PlaybackScheduler scheduler;
    double bpm = 120.0;
    double sr = 48000.0;
    std::vector<Logged> log;

    explicit Rig(const std::vector<NoteEvent>& notes) { scheduler.setNotes(notes); }

    /// One block; position given as an absolute sample on the song timeline.
    const std::vector<ScheduledEvent>& block(bool playing, long long absSample, int n) {
        TransportBlock tb;
        tb.playing = playing;
        tb.ppqStart = static_cast<double>(absSample) * bpm / (60.0 * sr);
        tb.bpm = bpm;
        tb.sampleRate = sr;
        tb.numSamples = n;
        const auto& ev = scheduler.process(tb);
        for (const auto& e : ev) {
            REQUIRE(e.sampleOffset >= 0);
            REQUIRE(e.sampleOffset < n);
            log.push_back({absSample + e.sampleOffset, e.noteOn, e.pitch, e.velocity, e.channel});
        }
        return ev;
    }

    /// Plays [from, to) contiguously with fixed-size blocks.
    void play(long long from, long long to, int blockSize) {
        for (long long s = from; s < to; s += blockSize) block(true, s, blockSize);
    }
};

std::vector<NoteEvent> demoNotes() { return generateNotes(makeDemoProgression()); }

/// The log the demo progression must produce at a given tempo/sample rate: on+off per note.
std::vector<Logged> expectedDemoLog(double bpm, double sr) {
    std::vector<Logged> out;
    for (const auto& n : demoNotes()) {
        out.push_back({expectedSample(static_cast<double>(n.startTick), bpm, sr), true, n.pitch, n.velocity, n.channel});
        out.push_back({expectedSample(static_cast<double>(n.startTick + n.lengthTicks), bpm, sr), false, n.pitch, 0, n.channel});
    }
    return out;
}

void sortLog(std::vector<Logged>& v) {
    std::sort(v.begin(), v.end(), [](const Logged& a, const Logged& b) {
        return std::make_tuple(a.sample, a.on, a.pitch) < std::make_tuple(b.sample, b.on, b.pitch);
    });
}

}  // namespace

TEST_CASE("scheduler: first block of the demo starts chord I at offset 0", "[scheduler]") {
    Rig rig(demoNotes());
    const auto& ev = rig.block(true, 0, 512);
    REQUIRE(ev.size() == 3);
    const int pitches[3] = {48, 52, 55};
    for (int i = 0; i < 3; ++i) {
        CHECK(ev[i].sampleOffset == 0);
        CHECK(ev[i].noteOn);
        CHECK(ev[i].pitch == pitches[i]);
        CHECK(ev[i].velocity == 96);
        CHECK(ev[i].channel == 0);
    }
    CHECK(rig.scheduler.activeNoteCount() == 3);
}

TEST_CASE("scheduler: whole demo lands on exact samples (120 BPM, 48 kHz)", "[scheduler][ticks]") {
    // 3840 ticks = 4 quarters = 2 s = 96000 samples, so chords start at 0, 96000, 192000, 288000.
    for (int blockSize : {512, 480, 1000, 64}) {
        INFO("block size " << blockSize);
        Rig rig(demoNotes());
        rig.play(0, 384000 + 4096, blockSize);

        auto actual = rig.log;
        auto expected = expectedDemoLog(120.0, 48000.0);
        REQUIRE(actual.size() == 24);
        // hand-checked anchors
        CHECK(expectedSample(3840, 120, 48000) == 96000);
        CHECK(expectedSample(15360, 120, 48000) == 384000);

        sortLog(actual);
        sortLog(expected);
        CHECK(actual == expected);
        CHECK(rig.scheduler.activeNoteCount() == 0);
    }
}

TEST_CASE("scheduler: repeated pitch re-triggers (off before on at the same sample)", "[scheduler][order]") {
    // I (48 52 55) then V (55 59 62): pitch 55 ends and starts at sample 96000.
    Rig rig(demoNotes());
    rig.play(0, 100000, 512);
    std::vector<Logged> at;
    for (const auto& l : rig.log) if (l.sample == 96000) at.push_back(l);
    REQUIRE(at.size() == 6);
    int lastOff = -1, firstOn = 99;
    for (int i = 0; i < 6; ++i) {
        if (!at[i].on) lastOff = i; else firstOn = std::min(firstOn, i);
    }
    CHECK(lastOff < firstOn);
}

TEST_CASE("scheduler: tempo scales sample positions (90 BPM)", "[scheduler][tempo]") {
    Rig rig(demoNotes());
    rig.bpm = 90.0;
    // 3840 ticks at 90 BPM = 4 quarters * 2/3 s = 2.6667 s = 128000 samples
    CHECK(expectedSample(3840, 90, 48000) == 128000);
    rig.play(0, 512000 + 4096, 512);
    auto actual = rig.log;
    auto expected = expectedDemoLog(90.0, 48000.0);
    sortLog(actual);
    sortLog(expected);
    CHECK(actual == expected);
}

TEST_CASE("scheduler: other sample rates", "[scheduler][tempo]") {
    for (double sr : {44100.0, 96000.0}) {
        INFO("sample rate " << sr);
        Rig rig(demoNotes());
        rig.sr = sr;
        rig.play(0, static_cast<long long>(sr * 8.0) + 4096, 333);
        auto actual = rig.log;
        auto expected = expectedDemoLog(120.0, sr);
        sortLog(actual);
        sortLog(expected);
        CHECK(actual == expected);
    }
}

TEST_CASE("scheduler: stopping releases every sounding note at offset 0", "[scheduler][stop]") {
    Rig rig(demoNotes());
    rig.play(0, 24000, 512);  // half a second into chord I
    REQUIRE(rig.scheduler.activeNoteCount() == 3);

    const auto& ev = rig.block(false, 24064, 512);
    REQUIRE(ev.size() == 3);
    for (const auto& e : ev) {
        CHECK_FALSE(e.noteOn);
        CHECK(e.sampleOffset == 0);
    }
    CHECK(rig.scheduler.activeNoteCount() == 0);

    // stopped: silence, and no events however long it stays stopped
    CHECK(rig.block(false, 24064, 512).empty());
    CHECK(rig.block(false, 24064, 512).empty());
}

TEST_CASE("scheduler: a loop wrap releases and restarts cleanly", "[scheduler][loop]") {
    Rig rig(demoNotes());
    rig.play(0, 48000, 512);  // 1 s = ppq 2.0, middle of chord I
    REQUIRE(rig.scheduler.activeNoteCount() == 3);

    const auto& ev = rig.block(true, 0, 512);  // host loops back to the start
    REQUIRE(ev.size() == 6);
    for (int i = 0; i < 3; ++i) {
        CHECK_FALSE(ev[i].noteOn);  // old notes released first
        CHECK(ev[i].sampleOffset == 0);
    }
    for (int i = 3; i < 6; ++i) {
        CHECK(ev[i].noteOn);        // chord I retriggered
        CHECK(ev[i].sampleOffset == 0);
    }
    CHECK(rig.scheduler.activeNoteCount() == 3);
}

TEST_CASE("scheduler: a locate into another chord switches chords", "[scheduler][locate]") {
    Rig rig(demoNotes());
    rig.play(0, 12000, 512);                       // chord I sounding
    const auto& ev = rig.block(true, 200000, 512);  // jump to ~4.17 s: inside chord V (96000..192000)? no: chord vi
    // 200000 samples = 8000 ticks -> chord vi (A C E = 57 60 64), 7680..11520
    int offs = 0, ons = 0;
    std::vector<int> onPitches;
    for (const auto& e : ev) {
        if (e.noteOn) { ++ons; onPitches.push_back(e.pitch); } else { ++offs; }
        CHECK(e.sampleOffset == 0);
    }
    CHECK(offs == 3);
    CHECK(ons == 3);
    CHECK(onPitches == std::vector<int>{57, 60, 64});
}

TEST_CASE("scheduler: starting playback mid-chord sounds the chord immediately", "[scheduler][locate]") {
    Rig rig(demoNotes());
    // ppq 6 = tick 5760 = sample 144000, inside chord V (3840..7680)
    const auto& ev = rig.block(true, 144000, 512);
    REQUIRE(ev.size() == 3);
    const int pitches[3] = {55, 59, 62};
    for (int i = 0; i < 3; ++i) {
        CHECK(ev[i].noteOn);
        CHECK(ev[i].sampleOffset == 0);
        CHECK(ev[i].pitch == pitches[i]);
    }
    // and they end where the chord ends: tick 7680 = sample 192000
    rig.play(144000 + 512, 200000, 512);
    int offsAt192000 = 0;
    for (const auto& l : rig.log) if (!l.on && l.sample == 192000 && l.pitch != 55) ++offsAt192000;
    CHECK(offsAt192000 == 2);  // 59 and 62 end there (55 ends and restarts)
}

TEST_CASE("scheduler: negative positions (count-in) are silent, then playback starts on time", "[scheduler][countin]") {
    Rig rig(demoNotes());
    long long s = -96000;
    for (; s + 512 <= 0; s += 512) CHECK(rig.block(true, s, 512).empty());  // blocks wholly before 0
    // The next block crosses sample 0, so it contains the chord I start, sample-accurately.
    const auto& ev = rig.block(true, s, 512);
    REQUIRE(ev.size() == 3);
    for (const auto& e : ev) {
        CHECK(e.noteOn);
        CHECK(e.sampleOffset == -s);
    }
}

TEST_CASE("scheduler: nothing to play, or nothing after the end", "[scheduler]") {
    Rig empty({});
    CHECK(empty.block(true, 0, 512).empty());
    CHECK(empty.block(true, 512, 512).empty());

    Rig rig(demoNotes());
    CHECK(rig.block(true, 400000, 512).empty());  // beyond the 4 bars
    CHECK(rig.scheduler.activeNoteCount() == 0);
}

TEST_CASE("scheduler: invalid blocks are ignored and leave state untouched", "[scheduler]") {
    Rig rig(demoNotes());
    rig.block(true, 0, 512);
    REQUIRE(rig.scheduler.activeNoteCount() == 3);

    TransportBlock bad;
    bad.playing = true;
    for (int variant = 0; variant < 5; ++variant) {
        bad.numSamples = variant == 0 ? 0 : 512;
        bad.sampleRate = variant == 1 ? 0.0 : 48000.0;
        bad.bpm = variant == 2 ? 0.0 : 120.0;
        bad.ppqStart = variant == 3 ? std::nan("") : 0.0;
        bad.bpm = variant == 4 ? std::nan("") : bad.bpm;
        CHECK(rig.scheduler.process(bad).empty());
        CHECK(rig.scheduler.activeNoteCount() == 3);
    }
}

TEST_CASE("scheduler: very short notes never get their off before their on", "[scheduler][edge]") {
    // 1000 Hz sample rate at 240 BPM = 3.84 ticks per sample, so a 1-tick note is shorter than a sample.
    NoteEvent n{60, 100, 1, 90, 0};
    {
        Rig rig({n});
        rig.sr = 1000.0;
        rig.bpm = 240.0;
        const auto& ev = rig.block(true, 0, 100);
        REQUIRE(ev.size() == 2);
        CHECK(ev[0].noteOn);
        CHECK_FALSE(ev[1].noteOn);
        CHECK(ev[1].sampleOffset > ev[0].sampleOffset);
        CHECK(rig.scheduler.activeNoteCount() == 0);
    }
    {
        // the note starts on the very last sample of the block: its off must move to the next block
        Rig rig({n});
        rig.sr = 1000.0;
        rig.bpm = 240.0;
        const auto& first = rig.block(true, 0, 27);
        REQUIRE(first.size() == 1);
        CHECK(first[0].noteOn);
        CHECK(first[0].sampleOffset == 26);
        CHECK(rig.scheduler.activeNoteCount() == 1);
        const auto& second = rig.block(true, 27, 27);
        REQUIRE(second.size() == 1);
        CHECK_FALSE(second[0].noteOn);
        CHECK(second[0].sampleOffset == 0);
        CHECK(rig.scheduler.activeNoteCount() == 0);
    }
}

TEST_CASE("scheduler: playback agrees with the exported MIDI file (one engine)", "[scheduler][engine]") {
    const auto progression = makeDemoProgression();
    const auto file = smf::parse(exportProgressionToMidi(progression));
    const auto exported = smf::extractNotes(file.tracks[0]);

    Rig rig(generateNotes(progression));
    rig.play(0, 384000 + 2048, 777);

    // Rebuild notes from the playback stream. At 120 BPM / 48 kHz one sample is 0.04 tick, so
    // tick = sample / 25 exactly.
    struct Rebuilt { int pitch; long long start, length; int velocity, channel; };
    std::vector<Rebuilt> rebuilt;
    std::map<std::pair<int, int>, std::pair<long long, int>> open;
    for (const auto& l : rig.log) {
        const auto key = std::make_pair(l.channel, l.pitch);
        if (l.on) {
            open[key] = {l.sample, l.velocity};
        } else {
            const auto it = open.find(key);
            REQUIRE(it != open.end());
            rebuilt.push_back({l.pitch, it->second.first / 25, (l.sample - it->second.first) / 25, it->second.second, l.channel});
            open.erase(it);
        }
    }
    CHECK(open.empty());
    std::sort(rebuilt.begin(), rebuilt.end(), [](const Rebuilt& a, const Rebuilt& b) {
        return std::tie(a.start, a.pitch) < std::tie(b.start, b.pitch);
    });

    REQUIRE(rebuilt.size() == exported.size());
    for (std::size_t i = 0; i < exported.size(); ++i) {
        INFO("note " << i);
        CHECK(rebuilt[i].pitch == exported[i].pitch);
        CHECK(rebuilt[i].start == static_cast<long long>(exported[i].start));
        CHECK(rebuilt[i].length == static_cast<long long>(exported[i].length));
        CHECK(rebuilt[i].velocity == exported[i].velocity);
        CHECK(rebuilt[i].channel == exported[i].channel);
    }
}

TEST_CASE("property: random transports never leave a stuck note and stay on the sample grid", "[scheduler][property]") {
    std::mt19937 rng(0xBEEF);
    auto pick = [&](unsigned n) { return static_cast<int>(rng() % n); };
    const double rates[] = {44100.0, 48000.0, 88200.0, 96000.0};

    for (int iteration = 0; iteration < 200; ++iteration) {
        INFO("iteration " << iteration);
        const auto progression = [&] {
            auto p = makeDemoProgression();
            p.blocks.clear();
            Tick cursor = 60 * pick(20);
            for (int b = 0, n = 1 + pick(6); b < n; ++b) {
                ChordSpec c{1 + pick(7), 0, Quality::Major, pick(2) ? Seventh::Minor : Seventh::None, 0, Suspension::None};
                const Tick dur = 60 * (1 + pick(100));
                p.blocks.push_back(ChordBlock{c, cursor, dur});
                cursor += dur + (pick(3) == 0 ? 60 * pick(10) : 0);
            }
            return p;
        }();
        const auto notes = generateNotes(progression);

        Rig rig(notes);
        rig.bpm = 40 + pick(200);
        rig.sr = rates[pick(4)];

        const long long end = expectedSample(static_cast<double>(progression.endTick()), rig.bpm, rig.sr) + 8192;
        for (long long s = 0; s < end;) {
            const int n = 1 + pick(2048);
            rig.block(true, s, n);
            s += n;
        }
        CHECK(rig.scheduler.activeNoteCount() == 0);

        // every note sounded exactly once, within one sample of its theoretical position
        std::vector<Logged> expected;
        for (const auto& note : notes) {
            expected.push_back({expectedSample(static_cast<double>(note.startTick), rig.bpm, rig.sr), true, note.pitch, note.velocity, note.channel});
            expected.push_back({expectedSample(static_cast<double>(note.startTick + note.lengthTicks), rig.bpm, rig.sr), false, note.pitch, 0, note.channel});
        }
        REQUIRE(rig.log.size() == expected.size());
        auto actual = rig.log;
        // Compare per (pitch, on) occurrence in time order, tolerating +-1 sample from rounding (and +1
        // when a sub-sample note had its off nudged after its on).
        auto key = [](const Logged& l) { return std::make_tuple(l.pitch, l.on, l.sample); };
        std::sort(actual.begin(), actual.end(), [&](auto& a, auto& b) { return key(a) < key(b); });
        std::sort(expected.begin(), expected.end(), [&](auto& a, auto& b) { return key(a) < key(b); });
        for (std::size_t i = 0; i < actual.size(); ++i) {
            REQUIRE(actual[i].pitch == expected[i].pitch);
            REQUIRE(actual[i].on == expected[i].on);
            REQUIRE(std::llabs(actual[i].sample - expected[i].sample) <= 1);
        }
    }
}

TEST_CASE("scheduler: swapping the note list mid-chord releases the sounding notes at offset 0", "[scheduler][edit]") {
    PlaybackScheduler s;
    const auto demo = makeDemoProgression();
    s.setNotes(generateNotes(demo));

    // Start inside bar 1: the first chord's notes sound.
    const auto& on = s.process(TransportBlock{true, 0.0, 120.0, 48000.0, 512});
    std::vector<std::pair<int, int>> sounding;  // pitch, channel
    for (const auto& e : on) if (e.noteOn) sounding.emplace_back(e.pitch, e.channel);
    REQUIRE_FALSE(sounding.empty());
    REQUIRE(s.activeNoteCount() == sounding.size());

    // The user edits the progression while the transport is rolling.
    auto edited = demo;
    edited.key.tonic = 2;
    s.setNotes(generateNotes(edited));

    // Next block (transport continues): every orphaned note gets exactly one note-off at offset 0.
    const auto& next = s.process(TransportBlock{true, 512.0 / 48000.0 * 2.0, 120.0, 48000.0, 512});
    std::vector<std::pair<int, int>> released;
    for (const auto& e : next) {
        if (!e.noteOn && e.sampleOffset == 0) released.emplace_back(e.pitch, e.channel);
    }
    std::sort(sounding.begin(), sounding.end());
    std::sort(released.begin(), released.end());
    CHECK(released == sounding);
}

TEST_CASE("scheduler: orphaned notes are released even if the transport has stopped", "[scheduler][edit]") {
    PlaybackScheduler s;
    s.setNotes(generateNotes(makeDemoProgression()));
    const auto& on = s.process(TransportBlock{true, 0.0, 120.0, 48000.0, 512});
    const auto count = std::count_if(on.begin(), on.end(), [](const ScheduledEvent& e) { return e.noteOn; });
    REQUIRE(count > 0);

    s.setNotes(generateNotes(makeDemoProgression()));
    const auto& out = s.process(TransportBlock{false, 0.0, 120.0, 48000.0, 512});
    const auto offs = std::count_if(out.begin(), out.end(), [](const ScheduledEvent& e) { return !e.noteOn && e.sampleOffset == 0; });
    CHECK(offs == count);
    CHECK(std::none_of(out.begin(), out.end(), [](const ScheduledEvent& e) { return e.noteOn; }));

    // And only once.
    CHECK(s.process(TransportBlock{false, 0.0, 120.0, 48000.0, 512}).empty());
}

TEST_CASE("scheduler: after a swap the new notes start cleanly from the transport position", "[scheduler][edit]") {
    PlaybackScheduler s;
    s.setNotes(generateNotes(makeDemoProgression()));
    s.process(TransportBlock{true, 0.0, 120.0, 48000.0, 512});

    auto edited = makeDemoProgression();
    edited.key.tonic = 7;
    const auto newNotes = generateNotes(edited);
    s.setNotes(newNotes);

    // Transport is still mid-bar-1: the new chord's notes start at offset 0 alongside the releases.
    const auto& out = s.process(TransportBlock{true, 0.1, 120.0, 48000.0, 512});
    std::vector<int> started;
    for (const auto& e : out) if (e.noteOn) { started.push_back(e.pitch); CHECK(e.sampleOffset == 0); }
    std::vector<int> expected;
    for (const auto& n : newNotes) if (n.startTick == 0) expected.push_back(n.pitch);
    std::sort(started.begin(), started.end());
    std::sort(expected.begin(), expected.end());
    CHECK(started == expected);
}
