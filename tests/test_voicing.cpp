// Chord Density 2.0: Auto restores the original voicing, voice reduction/expansion, register, distribution,
// preserve bass, voice leading, timing, persistence.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>
#include <chordcore/Serialization.h>
#include <chordcore/Voicing.h>

#include <algorithm>
#include <cstdlib>
#include <set>

using namespace chordcore;

namespace {

const Key kC{0, Mode::Major};

ChordSpec triadC() { return ChordSpec{}; }  // I = C major
ChordSpec c7() {
    ChordSpec c;
    c.seventh = Seventh::Minor;  // C E G Bb
    return c;
}
ChordSpec fm9() {
    ChordSpec c;
    c.degree = 4;
    c.quality = Quality::Minor;
    c.seventh = Seventh::Minor;
    c.ninth = Ninth::Major;  // F Ab C Eb G
    return c;
}

std::set<int> pitchClasses(const std::vector<int>& pitches) {
    std::set<int> out;
    for (int p : pitches) out.insert(p % 12);
    return out;
}

bool ascendingDistinct(const std::vector<int>& p) {
    for (std::size_t i = 1; i < p.size(); ++i) {
        if (p[i] <= p[i - 1]) return false;
    }
    return true;
}

VoicingSettings dense(int n, Distribution d = Distribution::Automatic) {
    VoicingSettings s;
    s.density = n;
    s.distribution = d;
    return s;
}

std::vector<int> voice(const ChordSpec& c, const VoicingSettings& s, const std::vector<int>* prev = nullptr) {
    return voiceChordWithSettings(kC, c, VoicingOptions{}, s, 0, prev).pitches();
}

int movement(const std::vector<int>& a, const std::vector<int>& b) {
    int total = 0;
    for (int x : a) {
        int best = 1000;
        for (int y : b) best = std::min(best, std::abs(x - y));
        total += best;
    }
    return total;
}

Progression threeChords(const VoicingSettings& global) {
    Progression p;
    p.blocks.push_back(ChordBlock{triadC(), 0, 3840});
    ChordSpec f;
    f.degree = 4;
    p.blocks.push_back(ChordBlock{f, 3840, 3840});
    ChordSpec g;
    g.degree = 5;
    g.seventh = Seventh::Minor;
    p.blocks.push_back(ChordBlock{g, 7680, 3840});
    p.voicing = global;
    return p;
}

}  // namespace

TEST_CASE("density: Auto restores the original voicing exactly", "[voicing]") {
    for (int inversion = 0; inversion <= 2; ++inversion) {
        ChordSpec c = triadC();
        c.inversion = inversion;
        const auto original = voiceChord(kC, c, VoicingOptions{});
        const auto voiced = voiceChordWithSettings(kC, c, VoicingOptions{}, VoicingSettings{}, 0);
        CHECK(voiced.pitches() == original);
        CHECK(voiced.original);
        CHECK(voiced.actualVoices == static_cast<int>(original.size()));
        for (const auto& n : voiced.notes) CHECK(n.original);
    }
    for (const auto& c : {c7(), fm9()}) {
        CHECK(voice(c, VoicingSettings{}) == voiceChord(kC, c, VoicingOptions{}));
    }
    // Going back to Auto after a dense setting gives the original again (the chord is never modified).
    auto s = dense(8, Distribution::Spread);
    CHECK(voice(fm9(), s) != voiceChord(kC, fm9(), VoicingOptions{}));
    s.density = 0;
    CHECK(voice(fm9(), s) == voiceChord(kC, fm9(), VoicingOptions{}));
}

TEST_CASE("density: a triad expands to 6 distinct voices by octave doublings of chord tones", "[voicing]") {
    for (auto d : {Distribution::Automatic, Distribution::Close, Distribution::Open, Distribution::Spread}) {
        const auto pitches = voice(triadC(), dense(6, d));
        INFO(toString(d));
        REQUIRE(pitches.size() == 6);
        CHECK(ascendingDistinct(pitches));
        CHECK(pitchClasses(pitches) == std::set<int>({0, 4, 7}));  // no unrelated notes
    }
}

TEST_CASE("density: every density 1..12 gives that many distinct in-range chord-tone pitches", "[voicing]") {
    const std::set<int> c7pcs{0, 4, 7, 10};
    for (int n = 1; n <= kMaxDensity; ++n) {
        for (auto d : {Distribution::Automatic, Distribution::Close, Distribution::Open, Distribution::Spread}) {
            for (auto reg : {RegisterPreset::Low, RegisterPreset::Mid, RegisterPreset::High, RegisterPreset::Full}) {
                auto s = dense(n, d);
                s.registerPreset = reg;
                const auto v = voiceChordWithSettings(kC, c7(), VoicingOptions{}, s, 0);
                INFO("n=" << n << " " << toString(d) << " " << toString(reg));
                CHECK(v.actualVoices == n);
                CHECK(v.targetVoices == n);
                CHECK(ascendingDistinct(v.pitches()));
                for (int p : v.pitches()) {
                    CHECK(p >= v.range.low);
                    CHECK(p <= v.range.high);
                    CHECK(c7pcs.count(p % 12) == 1);
                }
            }
        }
    }
}

TEST_CASE("density: C7 at 3 voices is the meaningful C-E-Bb", "[voicing]") {
    const auto v = voiceChordWithSettings(kC, c7(), VoicingOptions{}, dense(3), 0);
    CHECK(v.pitches() == std::vector<int>({48, 52, 58}));  // C E Bb, the fifth is dropped
    CHECK(v.actualVoices == 3);
    CHECK(v.originalVoices == 4);
    // two voices: root + third; one voice: the bass
    CHECK(pitchClasses(voice(c7(), dense(2))) == std::set<int>({0, 4}));
    CHECK(voice(c7(), dense(1)) == std::vector<int>({48}));
}

TEST_CASE("density: Fm9 keeps its extension tones when reduced and spreads across octaves when expanded", "[voicing]") {
    // 4 voices: F, Ab, Eb and the 9th (G); the plain fifth (C) is what goes.
    const auto four = voice(fm9(), dense(4));
    CHECK(pitchClasses(four) == std::set<int>({5, 8, 3, 7}));
    // exactly the original at 5 voices
    CHECK(voice(fm9(), dense(5)) == voiceChord(kC, fm9(), VoicingOptions{}));
    // 8 voices: all five chord tones present, three octave doublings, nothing else
    const auto eight = voice(fm9(), dense(8, Distribution::Spread));
    REQUIRE(eight.size() == 8);
    CHECK(pitchClasses(eight) == std::set<int>({5, 8, 0, 3, 7}));
    CHECK(eight.back() - eight.front() > 12);  // spread over more than an octave
}

TEST_CASE("density: register presets and custom ranges bound every note", "[voicing]") {
    for (int shift = -2; shift <= 2; ++shift) {
        for (auto reg : {RegisterPreset::Low, RegisterPreset::Mid, RegisterPreset::High, RegisterPreset::Full}) {
            VoicingSettings s = dense(7, Distribution::Open);
            s.registerPreset = reg;
            VoicingOptions o;
            o.rootBase += 12 * shift;
            const auto v = voiceChordWithSettings(kC, fm9(), o, s, shift);
            CHECK(v.range.low >= 0);
            CHECK(v.range.high <= 127);
            for (int p : v.pitches()) {
                CHECK(p >= v.range.low);
                CHECK(p <= v.range.high);
            }
        }
    }
    VoicingSettings custom = dense(6, Distribution::Close);
    custom.registerPreset = RegisterPreset::Custom;
    custom.customLow = 60;
    custom.customHigh = 79;
    const auto pitches = voice(c7(), custom);
    REQUIRE(pitches.size() == 6);
    for (int p : pitches) {
        CHECK(p >= 60);
        CHECK(p <= 79);
    }
    CHECK(RegisterRange{registerRange(VoicingSettings{}, 0)}.low == 36);
    CHECK(registerRange(VoicingSettings{}, 1).low == 48);  // presets follow the octave shift
    CHECK(registerRange(custom, 2).low == 60);             // custom is absolute
}

TEST_CASE("density: a register too narrow for the request reports actual < target", "[voicing]") {
    VoicingSettings s = dense(12);
    s.registerPreset = RegisterPreset::Custom;
    s.customLow = 36;
    s.customHigh = 47;  // one octave: each chord tone fits exactly once
    const auto v = voiceChordWithSettings(kC, triadC(), VoicingOptions{}, s, 0);
    CHECK(v.actualVoices == 3);
    CHECK(v.targetVoices == 12);
    CHECK_FALSE(v.note.empty());
    CHECK(validateVoicingSettings([] { VoicingSettings x; x.registerPreset = RegisterPreset::Custom; x.customLow = 40; x.customHigh = 45; return x; }()) != "");
    CHECK(validateVoicingSettings(dense(13)) != "");
    CHECK(validateVoicingSettings(dense(12)).empty());
}

TEST_CASE("density: Preserve Bass keeps the inversion bass as the lowest voice", "[voicing]") {
    ChordSpec firstInversion = triadC();
    firstInversion.inversion = 1;  // C/E: bass E
    for (auto d : {Distribution::Automatic, Distribution::Close, Distribution::Open, Distribution::Spread}) {
        for (int n = 1; n <= 9; ++n) {
            VoicingSettings s = dense(n, d);
            s.preserveBass = true;
            const auto pitches = voice(firstInversion, s);
            INFO(toString(d) << " n=" << n);
            REQUIRE_FALSE(pitches.empty());
            CHECK(pitches.front() % 12 == 4);
        }
    }
    const auto v = voiceChordWithSettings(kC, firstInversion, VoicingOptions{}, dense(4), 0);
    CHECK(v.notes.front().role == VoiceRole::Bass);
    // Without Preserve Bass the voicing is built from the root instead.
    VoicingSettings free = dense(3, Distribution::Close);
    free.preserveBass = false;
    CHECK(voice(firstInversion, free).front() % 12 == 0);
}

TEST_CASE("density: voice metadata has stable ids and sensible roles", "[voicing]") {
    const auto a = voiceChordWithSettings(kC, fm9(), VoicingOptions{}, dense(5), 0);
    const auto b = voiceChordWithSettings(kC, fm9(), VoicingOptions{}, dense(9, Distribution::Spread), 0);
    auto idOfInterval = [](const VoicedChord& v, int interval, bool doubling) {
        for (const auto& n : v.notes) {
            if (n.interval == interval && n.doubling == doubling) return n.voiceId;
        }
        return -1;
    };
    for (int interval : {0, 3, 7, 10, 14}) {
        CHECK(idOfInterval(a, interval, false) >= 0);
        CHECK(idOfInterval(a, interval, false) == idOfInterval(b, interval, false));  // same tone, same id
    }
    std::set<int> ids;
    for (const auto& n : b.notes) CHECK(ids.insert(n.voiceId).second);  // ids are unique within a chord
    CHECK(b.notes.front().role == VoiceRole::Bass);
    for (const auto& n : b.notes) {
        if (n.interval == 14 && n.role != VoiceRole::Bass) CHECK(n.role == VoiceRole::Extension);
    }
    CHECK(std::count_if(b.notes.begin(), b.notes.end(), [](const VoicedNote& n) { return n.doubling; }) == 4);
}

TEST_CASE("density: voice leading is deterministic and never moves more than without it", "[voicing]") {
    VoicingSettings off = dense(4, Distribution::Close);
    VoicingSettings on = off;
    on.voiceLeading = true;
    for (bool preserve : {true, false}) {
        off.preserveBass = on.preserveBass = preserve;
        const auto without = voiceProgression(threeChords(off));
        const auto with1 = voiceProgression(threeChords(on));
        const auto with2 = voiceProgression(threeChords(on));
        int moveOff = 0, moveOn = 0;
        for (std::size_t i = 1; i < 3; ++i) {
            CHECK(with1[i].pitches() == with2[i].pitches());
            moveOff += movement(without[i].pitches(), without[i - 1].pitches());
            moveOn += movement(with1[i].pitches(), with1[i - 1].pitches());
        }
        INFO("preserve=" << preserve);
        CHECK(moveOn <= moveOff);
        for (const auto& v : with1) {
            CHECK(v.actualVoices == 4);
            CHECK(ascendingDistinct(v.pitches()));
        }
    }
    // The first chord has no previous chord and is voiced as without voice leading.
    CHECK(voiceProgression(threeChords(on))[0].pitches() == voiceProgression(threeChords(off))[0].pitches());
}

TEST_CASE("density: voice leading changes the placement when an octave jump would otherwise happen", "[voicing]") {
    // From a high previous voicing the new chord should follow it up instead of restarting low.
    VoicingSettings s = dense(3, Distribution::Close);
    s.voiceLeading = true;
    s.registerPreset = RegisterPreset::Full;
    const std::vector<int> high{72, 76, 79};
    const auto followed = voice(triadC(), s, &high);
    s.voiceLeading = false;
    const auto restarted = voice(triadC(), s, &high);
    CHECK(movement(followed, high) < movement(restarted, high));
}

TEST_CASE("density: timing, velocity and melody are untouched", "[voicing]") {
    auto base = threeChords(VoicingSettings{});
    auto dens = threeChords(dense(6, Distribution::Spread));
    GenerationOptions o;
    o.velocity = 88;
    const auto a = generateNotes(base, o);
    const auto b = generateNotes(dens, o);
    auto groups = [](const std::vector<NoteEvent>& notes) {
        std::set<std::tuple<Tick, Tick, int>> g;
        for (const auto& n : notes) g.insert({n.startTick, n.lengthTicks, n.velocity});
        return g;
    };
    CHECK(groups(a) == groups(b));
    CHECK(b.size() == 18);  // 3 chords x 6 voices
    // a "&" of beat 2 entry stays on tick 1440 and the melody notes are not touched by density
    dens.melody.push_back(MelodyNote{64, 1440, 480, 90, false});
    auto sameMelody = dens;
    sameMelody.voicing = VoicingSettings{};
    CHECK(generateMelodyNotes(dens) == generateMelodyNotes(sameMelody));
    CHECK(generateMelodyNotes(dens).front().startTick == 1440);
    CHECK(dens.melody == sameMelody.melody);
}

TEST_CASE("density: per-chord settings override the global ones and originalVoicing ignores both", "[voicing]") {
    auto p = threeChords(dense(4, Distribution::Close));
    p.blocks[1].voicing = dense(2);
    const auto voiced = voiceProgression(p);
    CHECK(voiced[0].actualVoices == 4);
    CHECK(voiced[1].actualVoices == 2);
    CHECK(voiced[2].actualVoices == 4);
    const auto original = voiceProgression(p, VoicingOptions{}, /*originalVoicing=*/true);
    CHECK(original[0].pitches() == voiceChord(kC, p.blocks[0].chord, VoicingOptions{}));
    GenerationOptions o;
    o.originalVoicing = true;
    CHECK(generateNotes(p, o).size() == 3 + 3 + 4);
    CHECK(generateNotes(p).size() == 4 + 2 + 4);
}

TEST_CASE("density: playback pitches equal the generator pitches (one engine)", "[voicing]") {
    auto p = threeChords(dense(5, Distribution::Open));
    p.blocks[2].voicing = dense(3);
    const auto events = generateNotes(p);
    const auto voiced = voiceProgression(p);
    std::vector<int> fromVoiced;
    for (const auto& v : voiced) for (int x : v.pitches()) fromVoiced.push_back(x);
    std::vector<int> fromEvents;
    for (const auto& e : events) fromEvents.push_back(e.pitch);
    std::sort(fromVoiced.begin(), fromVoiced.end());
    std::sort(fromEvents.begin(), fromEvents.end());
    CHECK(fromVoiced == fromEvents);
    for (const auto& e : generateProjectNotes(p)) CHECK(e.channel == 0);  // chord channel 1 unchanged
}

TEST_CASE("density: settings persist; legacy projects load and re-save byte-identically", "[voicing][serialization]") {
    const Progression legacy = makeDemoProgression();
    const std::string text = serializeProgression(legacy);
    CHECK(text.find("voicing") == std::string::npos);  // nothing written for default settings
    const auto loaded = deserializeProgression(text);
    REQUIRE(loaded.ok);
    CHECK(loaded.progression.voicing == VoicingSettings{});
    CHECK(serializeProgression(loaded.progression) == text);

    auto p = threeChords(dense(7, Distribution::Spread));
    p.voicing.registerPreset = RegisterPreset::Custom;
    p.voicing.customLow = 30;
    p.voicing.customHigh = 100;
    p.voicing.preserveBass = false;
    p.voicing.voiceLeading = true;
    p.blocks[1].voicing = dense(2, Distribution::Close);
    const auto round = deserializeProgression(serializeProgression(p));
    REQUIRE(round.ok);
    CHECK(round.progression == p);
    CHECK(generateNotes(round.progression) == generateNotes(p));

    auto bad = p;
    bad.voicing.density = 13;
    CHECK_FALSE(bad.validate().empty());
    CHECK_THROWS(serializeProgression(bad));
    std::string tampered = serializeProgression(p);
    const auto at = tampered.find("\"density\":7");
    REQUIRE(at != std::string::npos);
    tampered.replace(at, 11, "\"density\":13");
    CHECK_FALSE(deserializeProgression(tampered).ok);
}

TEST_CASE("density: expansion doubles the root first, then the fifth, then the third", "[voicing]") {
    auto count = [](const std::vector<int>& pitches, int pc) {
        return std::count_if(pitches.begin(), pitches.end(), [pc](int p) { return p % 12 == pc; });
    };
    const auto four = voice(triadC(), dense(4));
    CHECK(count(four, 0) == 2);
    CHECK(count(four, 7) == 1);
    const auto five = voice(triadC(), dense(5));
    CHECK(count(five, 0) == 2);
    CHECK(count(five, 7) == 2);
    CHECK(count(five, 4) == 1);
    const auto six = voice(triadC(), dense(6));
    CHECK(count(six, 4) == 2);
}
