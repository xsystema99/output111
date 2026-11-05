// Rule-based melody generator: determinism, register, density, contour, locks, partial regeneration, persistence.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Document.h>
#include <chordcore/Harmony.h>
#include <chordcore/MelodyEdit.h>
#include <chordcore/MelodyGenerator.h>
#include <chordcore/Progression.h>
#include <chordcore/Serialization.h>

#include <algorithm>
#include <set>

using namespace chordcore;

namespace {

Progression demo() { return makeDemoProgression(); }  // C major I V vi IV, one bar each (3840 ticks)
constexpr Tick kBar = 3840;

Progression generated(GeneratorSettings g, int bars = 4) {
    auto p = demo();
    p.melodySettings.generator = g;
    GenerationRange r{0, kBar * bars};
    REQUIRE(regenerateMelody(p, r).ok);
    return p;
}

double meanPitch(const std::vector<MelodyNote>& notes, Tick from, Tick to) {
    double sum = 0;
    int n = 0;
    for (const auto& note : notes) {
        if (note.startTick >= from && note.startTick < to) { sum += note.pitch; ++n; }
    }
    return n == 0 ? -1.0 : sum / n;
}

}  // namespace

TEST_CASE("generator: deterministic for equal inputs, different for different seeds (test H)", "[generator]") {
    GeneratorSettings g;
    const auto a = generated(g);
    const auto b = generated(g);
    CHECK(a.melody == b.melody);
    CHECK(a == b);
    g.seed = 2;
    const auto c = generated(g);
    CHECK(c.melody != a.melody);
    REQUIRE_FALSE(a.melody.empty());
    CHECK(a.validate().empty());
}

TEST_CASE("generator: golden output for seed 1 (cross-platform determinism)", "[generator]") {
    const auto p = generated(GeneratorSettings{});
    std::string text;
    for (const auto& n : p.melody) text += std::to_string(n.startTick) + ":" + std::to_string(n.pitch) + ":" + std::to_string(n.durationTicks) + ":" + std::to_string(n.velocity) + " ";
    INFO(text);
    CHECK(text ==
          "0:60:1440:102 1440:64:480:82 1920:67:960:95 2880:72:960:84 3840:74:1440:99 5280:76:480:86 5760:79:960:91 6720:83:960:84 "
          "7680:84:1440:102 9120:83:480:80 9600:84:960:94 10560:81:960:86 11520:77:1440:103 12960:76:480:84 13440:77:960:98 14400:77:960:80 ");
}

TEST_CASE("generator: stays inside the register", "[generator]") {
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        GeneratorSettings g;
        g.seed = seed;
        g.registerLow = 65;
        g.registerHigh = 79;
        g.tension = 80;
        const auto p = generated(g);
        for (const auto& n : p.melody) {
            CHECK(n.pitch >= 65);
            CHECK(n.pitch <= 79);
        }
    }
    const auto def = generated(GeneratorSettings{});
    for (const auto& n : def.melody) {
        CHECK(n.pitch >= 60);  // default C4..C6
        CHECK(n.pitch <= 84);
    }
}

TEST_CASE("generator: density orders note counts", "[generator]") {
    std::size_t sparse = 0, moderate = 0, dense = 0;
    for (std::uint32_t seed = 1; seed <= 10; ++seed) {
        GeneratorSettings g;
        g.seed = seed;
        g.density = Density::Sparse;
        sparse += generated(g).melody.size();
        g.density = Density::Moderate;
        moderate += generated(g).melody.size();
        g.density = Density::Dense;
        dense += generated(g).melody.size();
    }
    CHECK(sparse < moderate);
    CHECK(moderate < dense);
}

TEST_CASE("generator: contour shapes the phrase", "[generator]") {
    int ascending = 0, descending = 0, arch = 0, inverted = 0;
    const int seeds = 20;
    for (std::uint32_t seed = 1; seed <= seeds; ++seed) {
        GeneratorSettings g;
        g.seed = seed;
        g.density = Density::Dense;
        g.tension = 20;
        g.contour = Contour::Ascending;
        auto m = generated(g).melody;
        if (meanPitch(m, kBar * 2, kBar * 4) > meanPitch(m, 0, kBar * 2)) ++ascending;
        g.contour = Contour::Descending;
        m = generated(g).melody;
        if (meanPitch(m, kBar * 2, kBar * 4) < meanPitch(m, 0, kBar * 2)) ++descending;
        g.contour = Contour::Arch;
        m = generated(g).melody;
        const double mid = meanPitch(m, kBar, kBar * 3);
        if (mid > meanPitch(m, 0, kBar) && mid > meanPitch(m, kBar * 3, kBar * 4)) ++arch;
        g.contour = Contour::InvertedArch;
        m = generated(g).melody;
        const double midLow = meanPitch(m, kBar, kBar * 3);
        if (midLow < meanPitch(m, 0, kBar) && midLow < meanPitch(m, kBar * 3, kBar * 4)) ++inverted;
    }
    CHECK(ascending >= seeds * 9 / 10);
    CHECK(descending >= seeds * 9 / 10);
    CHECK(arch >= seeds * 8 / 10);
    CHECK(inverted >= seeds * 8 / 10);
}

TEST_CASE("generator: zero tension uses only scale and chord tones, strong beats favour chord tones", "[generator]") {
    int strong = 0, strongChord = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        GeneratorSettings g;
        g.seed = seed;
        g.tension = 0;
        const auto p = generated(g);
        const auto regions = buildCompatibility(p);
        const auto scale = scalePitchClassMask(p.key);
        for (const auto& n : p.melody) {
            const auto* r = regionAt(regions, n.startTick);
            REQUIRE(r != nullptr);
            const int pc = n.pitch % 12;
            CHECK((((scale | r->chordMask) >> pc) & 1u) != 0);
            if (n.startTick % kBar == 0) {
                ++strong;
                if (((r->chordMask >> pc) & 1u) != 0) ++strongChord;
            }
        }
    }
    REQUIRE(strong > 0);
    CHECK(strongChord * 10 >= strong * 8);  // at least 80% of downbeats are chord tones
}

TEST_CASE("generator: phrase ends prefer the chord root", "[generator]") {
    int ends = 0, roots = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed) {
        GeneratorSettings g;
        g.seed = seed;
        const auto p = generated(g);
        const auto& last = p.melody.back();
        ++ends;
        const auto& chord = p.blocks[3].chord;  // IV, root F
        if (last.pitch % 12 == chordRootPitchClass(p.key, chord)) ++roots;
    }
    CHECK(roots * 10 >= ends * 6);
}

TEST_CASE("generator: output is valid and respects other tracks of time", "[generator]") {
    for (std::uint32_t seed = 1; seed <= 30; ++seed) {
        GeneratorSettings g;
        g.seed = seed;
        g.rhythm = static_cast<RhythmStyle>(seed % 5);
        g.density = static_cast<Density>(seed % 3);
        g.contour = static_cast<Contour>(seed % 6);
        g.tension = static_cast<int>((seed * 13) % 101);
        const auto p = generated(g);
        CHECK(p.validate().empty());
        for (const auto& n : p.melody) CHECK(n.endTick() <= kBar * 4);
    }
}

TEST_CASE("generator: other time signatures work", "[generator]") {
    auto p = demo();
    p.timeSignature = TimeSignature{6, 8};
    const Tick bar = p.timeSignature.ticksPerBar();
    for (auto& b : p.blocks) { b.startTick = b.startTick / kBar * bar; b.durationTicks = bar; }
    REQUIRE(p.validate().empty());
    REQUIRE(regenerateMelody(p, {0, bar * 4}).ok);
    CHECK_FALSE(p.melody.empty());
    CHECK(p.validate().empty());
}

TEST_CASE("generator: locked notes survive regeneration and are respected (test K)", "[generator]") {
    GeneratorSettings g;
    auto p = generated(g);
    REQUIRE(p.melody.size() >= 6);
    std::vector<MelodyNote> locked;
    for (std::size_t i : {std::size_t{1}, std::size_t{4}}) {
        p.melody[i].locked = true;
        locked.push_back(p.melody[i]);
    }
    p.melodySettings.generator.seed = 77;
    const auto r = regenerateMelody(p, {0, kBar * 4});
    REQUIRE(r.ok);
    for (const auto& l : locked) {
        CHECK(std::find(p.melody.begin(), p.melody.end(), l) != p.melody.end());
    }
    // nothing overlaps a locked note's time span
    for (const auto& l : locked) {
        for (const auto& n : p.melody) {
            if (n == l) continue;
            CHECK((n.endTick() <= l.startTick || n.startTick >= l.endTick()));
        }
    }
    CHECK(p.validate().empty());
}

TEST_CASE("generator: regenerating a region changes only that region", "[generator]") {
    GeneratorSettings g;
    auto p = generated(g);
    const auto before = p.melody;
    p.melodySettings.generator.seed = 5;
    REQUIRE(regenerateMelody(p, {kBar, kBar * 2}).ok);
    std::vector<MelodyNote> outsideBefore, outsideAfter;
    for (const auto& n : before) if (n.startTick < kBar || n.startTick >= kBar * 2) outsideBefore.push_back(n);
    for (const auto& n : p.melody) if (n.startTick < kBar || n.startTick >= kBar * 2) outsideAfter.push_back(n);
    CHECK(outsideBefore == outsideAfter);
    for (const auto& n : p.melody) {
        if (n.startTick >= kBar && n.startTick < kBar * 2) CHECK(n.endTick() <= kBar * 2);
    }
    CHECK(p.validate().empty());
}

TEST_CASE("generator: changing chords never touches the melody; affected regeneration targets only changed chords (test I)", "[generator]") {
    GeneratorSettings g;
    auto p = generated(g);
    CHECK(affectedRanges(p).empty());
    CHECK_FALSE(regenerateAffectedMelody(p).ok);  // nothing changed

    const auto melodyBefore = p.melody;
    p.blocks[1].chord = ChordSpec{2, 0, Quality::Minor, Seventh::None, 0};  // replace the V chord with ii
    CHECK(p.melody == melodyBefore);                                         // melody is never auto-changed
    const auto ranges = affectedRanges(p);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].start == kBar);
    CHECK(ranges[0].end == kBar * 2);

    p.melodySettings.generator.seed = 9;
    REQUIRE(regenerateAffectedMelody(p).ok);
    for (const auto& n : melodyBefore) {
        if (n.startTick < kBar || n.startTick >= kBar * 2) CHECK(std::find(p.melody.begin(), p.melody.end(), n) != p.melody.end());
    }
    CHECK(affectedRanges(p).empty());  // basis refreshed
    CHECK(p.validate().empty());

    // a hand-made melody has no basis: nothing to compare
    auto q = demo();
    REQUIRE(addMelodyNote(q, 64, 0, 480).ok);
    CHECK_FALSE(regenerateAffectedMelody(q).ok);
    q.blocks[0].chord = ChordSpec{4, 0, Quality::Major, Seventh::None, 0};
    CHECK(affectedRanges(q).empty());
}

TEST_CASE("generator: removed and added chords are affected too", "[generator]") {
    auto p = generated(GeneratorSettings{});
    p.blocks.pop_back();  // remove the last chord
    auto ranges = affectedRanges(p);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].start == kBar * 3);
    p = generated(GeneratorSettings{});
    ChordBlock extra;
    extra.chord = ChordSpec{1, 0, Quality::Major, Seventh::None, 0};
    extra.startTick = kBar * 4;
    extra.durationTicks = kBar;
    p.blocks.push_back(extra);
    ranges = affectedRanges(p);
    REQUIRE(ranges.size() == 1);
    CHECK(ranges[0].start == kBar * 4);
}

TEST_CASE("generator: undo and redo restore the melody and the basis (test K)", "[generator]") {
    Document d(demo());
    REQUIRE(d.edit("Generate", [](Progression& p) {
                 REQUIRE(regenerateMelody(p, {0, kBar * 4}).ok);
             }).ok);
    const auto generatedState = d.progression();
    CHECK_FALSE(generatedState.melody.empty());
    CHECK_FALSE(generatedState.melodyBasis.empty());
    REQUIRE(d.edit("Reroll", [](Progression& p) {
                 p.melodySettings.generator.seed += 1;
                 REQUIRE(regenerateMelody(p, {0, kBar * 4}).ok);
             }).ok);
    CHECK(d.progression().melody != generatedState.melody);
    REQUIRE(d.undo());
    CHECK(d.progression() == generatedState);
    REQUIRE(d.undo());
    CHECK(d.progression().melody.empty());
    CHECK(d.progression().melodyBasis.empty());
    REQUIRE(d.redo());
    CHECK(d.progression() == generatedState);
}

TEST_CASE("generator: scope ranges", "[generator]") {
    const auto p = demo();
    auto r = scopeRange(p, PhraseScope::TwoBars, 3, 0, 0);
    CHECK(r.start == kBar * 2);
    CHECK(r.end == kBar * 4);
    r = scopeRange(p, PhraseScope::FullProgression, 1, 0, 0);
    CHECK(r.start == 0);
    CHECK(r.end == kBar * 4);
    r = scopeRange(p, PhraseScope::SelectedRegion, 1, 960, 2880);
    CHECK(r.start == 960);
    CHECK(r.end == 2880);
    r = scopeRange(p, PhraseScope::SelectedRegion, 2, 0, 0);  // nothing selected: four bars from the start bar
    CHECK(r.start == kBar);
    CHECK(r.end == kBar * 5);
    r = scopeRange(p, PhraseScope::OneBar, 1000, 0, 0);       // clipped at the timeline end
    CHECK(r.start <= r.end);
}

TEST_CASE("generator: settings, seed and basis survive save and reload (test J)", "[generator]") {
    GeneratorSettings g;
    g.contour = Contour::Wave;
    g.density = Density::Dense;
    g.tension = 64;
    g.rhythm = RhythmStyle::Syncopated;
    g.scope = PhraseScope::EightBars;
    g.registerLow = 55;
    g.registerHigh = 91;
    g.seed = 4000000000u;
    auto p = generated(g);
    p.melody[0].locked = true;
    const auto text = serializeProgression(p);
    const auto r = deserializeProgression(text);
    REQUIRE(r.ok);
    CHECK(r.progression == p);
    CHECK(r.progression.melodySettings.generator == g);
    CHECK(r.progression.melodyBasis == p.melodyBasis);
    CHECK(serializeProgression(r.progression) == text);

    // defaults are not written
    auto plain = demo();
    CHECK(serializeProgression(plain).find("generator") == std::string::npos);

    // bad data is rejected
    auto bad = text;
    const auto pos = bad.find("\"tension\":64");
    REQUIRE(pos != std::string::npos);
    bad.replace(pos, 12, "\"tension\":999");
    CHECK_FALSE(deserializeProgression(bad).ok);
}
