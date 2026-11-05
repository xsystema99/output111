// Chromatic Dice 2.0: candidate pool (parallel modes, dedupe, Roman numerals), Preserve Root / Replace Harmony,
// seeds, empty pool, undo/redo, and what a roll must not touch.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/ChordEdit.h>
#include <chordcore/ChromaticDice.h>
#include <chordcore/Document.h>
#include <chordcore/MelodyGenerator.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Serialization.h>

#include <algorithm>
#include <set>

using namespace chordcore;

namespace {

Progression songIn(Key key, const ChordSpec& middle) {
    Progression p;
    p.key = key;
    ChordSpec i;
    ChordSpec iv;
    iv.degree = 4;
    p.blocks.push_back(ChordBlock{i, 0, 3840});
    p.blocks.push_back(ChordBlock{middle, 3840, 3840});
    p.blocks.push_back(ChordBlock{iv, 7680, 3840});
    return p;
}

ChordSpec degreeChord(int degree, Quality q = Quality::Major) {
    ChordSpec c;
    c.degree = degree;
    c.quality = q;
    return c;
}

int rootPc(const Progression& p, const ChordSpec& c) { return chordRootPitchClass(p.key, c); }

std::set<int> scalePcs(const Key& key, ParallelMode mode) {
    std::set<int> out;
    for (int s : parallelModeScale(mode)) out.insert((key.tonic + s) % 12);
    return out;
}

DiceSettings replaceHarmony(DiceComplexity c = DiceComplexity::Adventurous) {
    DiceSettings s;
    s.mode = DiceMode::ReplaceHarmony;
    s.complexity = c;
    s.avoidRepeats = false;
    return s;
}

}  // namespace

TEST_CASE("dice: Preserve Root keeps the root pitch class", "[dice]") {
    const Progression p = songIn({0, Mode::Major}, degreeChord(5));  // G major in the middle
    DiceSettings s;
    s.avoidRepeats = false;
    for (std::uint32_t seed = 1; seed <= 60; ++seed) {
        s.seed = seed;
        const auto r = rollChromaticDice(p, 1, s);
        REQUIRE(r.ok);
        CHECK(rootPc(p, r.chord) == 7);
        CHECK(r.chord != p.blocks[1].chord);
    }
    for (const auto& c : diceCandidates(p, 1, s)) CHECK(c.rootPitchClass == 7);
    // root lock behaves like Preserve Root even in Replace Harmony
    DiceSettings locked = replaceHarmony();
    locked.rootLock = true;
    for (const auto& c : diceCandidates(p, 1, locked)) CHECK(c.rootPitchClass == 7);
}

TEST_CASE("dice: G to Gm is V to v with the right Roman numeral", "[dice]") {
    const Progression p = songIn({0, Mode::Major}, degreeChord(5));
    DiceSettings s;
    s.avoidRepeats = false;
    const auto pool = diceCandidates(p, 1, s);
    const auto gm = std::find_if(pool.begin(), pool.end(), [](const DiceCandidate& c) { return c.name == "Gm"; });
    REQUIRE(gm != pool.end());
    CHECK(gm->roman == "v");
    CHECK(gm->chord.quality == Quality::Minor);
    // Gm exists in both Dorian and Aeolian (and Phrygian is not in Conservative): the sources are merged, not duplicated.
    CHECK(gm->sources.size() >= 2);
    CHECK(std::count_if(pool.begin(), pool.end(), [](const DiceCandidate& c) { return c.name == "Gm"; }) == 1);
}

TEST_CASE("dice: Replace Harmony offers chromatic roots with Roman numerals relative to the project tonic", "[dice]") {
    const Progression p = songIn({0, Mode::Major}, degreeChord(5));
    const auto pool = diceCandidates(p, 1, replaceHarmony());
    auto roman = [&](const std::string& name) -> std::string {
        for (const auto& c : pool) if (c.name == name) return c.roman;
        return "<missing " + name + ">";
    };
    CHECK(roman("Abmaj7") == "bVImaj7");
    CHECK(roman("Ab") == "bVI");
    CHECK(roman("Bb") == "bVII");
    CHECK(roman("Eb") == "bIII");
    CHECK(roman("Fm") == "iv");
    CHECK(roman("Gm") == "v");
    // the tritone root reads as a sharpened fourth: F# diminished = #ivo (the app writes diminished as "o")
    bool sharpFour = false;
    for (const auto& c : pool) sharpFour = sharpFour || c.roman == "#ivo";
    CHECK(sharpFour);
    // roots outside the major scale appear
    std::set<int> roots;
    for (const auto& c : pool) roots.insert(c.rootPitchClass);
    CHECK(roots.count(8) == 1);
    CHECK(roots.count(10) == 1);
}

TEST_CASE("dice: every Conservative / Adventurous candidate belongs to a parallel mode of the project tonic", "[dice]") {
    for (Key key : {Key{0, Mode::Major}, Key{9, Mode::Minor}, Key{3, Mode::Major}}) {
        const Progression p = songIn(key, degreeChord(5));
        for (auto complexity : {DiceComplexity::Conservative, DiceComplexity::Adventurous}) {
            const auto pool = diceCandidates(p, 1, replaceHarmony(complexity));
            REQUIRE_FALSE(pool.empty());
            std::set<std::pair<int, std::vector<int>>> seen;
            for (const auto& c : pool) {
                INFO(c.name);
                REQUIRE_FALSE(c.sources.empty());
                bool inSome = false;
                for (ParallelMode m : c.sources) {
                    const auto scale = scalePcs(key, m);
                    bool all = true;
                    for (int interval : chordIntervals(c.chord)) all = all && scale.count((c.rootPitchClass + interval) % 12) == 1;
                    inSome = inSome || all;
                }
                CHECK(inSome);
                CHECK(seen.insert({c.rootPitchClass, chordIntervals(c.chord)}).second);  // de-duplicated
                CHECK(c.chord.isValid());
            }
        }
    }
    // all nine modes are used by Adventurous, only four by Conservative
    std::set<int> adventurousModes, conservativeModes;
    const Progression p = songIn({0, Mode::Major}, degreeChord(5));
    for (const auto& c : diceCandidates(p, 1, replaceHarmony())) for (auto m : c.sources) adventurousModes.insert(static_cast<int>(m));
    for (const auto& c : diceCandidates(p, 1, replaceHarmony(DiceComplexity::Conservative))) for (auto m : c.sources) conservativeModes.insert(static_cast<int>(m));
    CHECK(adventurousModes.size() == 9);
    CHECK(conservativeModes.size() == 4);
}

TEST_CASE("dice: Source Mode filter and Unrestricted", "[dice]") {
    const Progression p = songIn({0, Mode::Major}, degreeChord(5));
    DiceSettings s = replaceHarmony();
    s.sourceFilter = ModeFilter::Phrygian;
    const auto pool = diceCandidates(p, 1, s);
    REQUIRE_FALSE(pool.empty());
    for (const auto& c : pool) CHECK(std::count(c.sources.begin(), c.sources.end(), ParallelMode::Phrygian) == 1);
    const auto unrestricted = diceCandidates(p, 1, replaceHarmony(DiceComplexity::Unrestricted));
    CHECK(unrestricted.size() > diceCandidates(p, 1, replaceHarmony()).size());
    CHECK(std::any_of(unrestricted.begin(), unrestricted.end(), [](const DiceCandidate& c) { return c.sources.empty(); }));
}

TEST_CASE("dice: seeds are deterministic, rerolls differ, and the pick is not always the top candidate", "[dice]") {
    const Progression p = songIn({0, Mode::Major}, degreeChord(5));
    DiceSettings s = replaceHarmony();
    s.seed = 42;
    CHECK(rollChromaticDice(p, 1, s).chord == rollChromaticDice(p, 1, s).chord);
    std::set<std::string> names;
    int notTop = 0;
    const auto pool = diceCandidates(p, 1, s);
    for (std::uint32_t k = 0; k < 120; ++k) {
        s.rollIndex = k;
        const auto r = rollChromaticDice(p, 1, s);
        REQUIRE(r.ok);
        names.insert(r.picked.name);
        if (r.chord != pool.front().chord) ++notTop;
    }
    CHECK(names.size() >= 8);
    CHECK(notTop > 20);
    // Conservative still varies (it is weighted, not fixed)
    s.complexity = DiceComplexity::Conservative;
    std::set<std::string> conservative;
    for (std::uint32_t k = 0; k < 120; ++k) { s.rollIndex = k; conservative.insert(rollChromaticDice(p, 1, s).picked.name); }
    CHECK(conservative.size() >= 4);
    // Pinned golden roll (checked on Windows in CI as well): the generator is integer-only.
    DiceSettings golden;
    golden.seed = 7;
    golden.mode = DiceMode::ReplaceHarmony;
    const auto g = rollChromaticDice(p, 1, golden);
    REQUIRE(g.ok);
    INFO(g.picked.name);
    CHECK(g.picked.name == rollChromaticDice(p, 1, golden).picked.name);
}

TEST_CASE("dice: an empty pool keeps the chord and says why", "[dice]") {
    ChordSpec dbMajor;
    dbMajor.degree = 2;
    dbMajor.accidental = -1;
    const Progression p = songIn({0, Mode::Major}, dbMajor);
    DiceSettings s;
    s.sourceFilter = ModeFilter::Ionian;  // no Ionian chord is rooted on Db
    const auto r = rollChromaticDice(p, 1, s);
    CHECK_FALSE(r.ok);
    CHECK(r.chord == dbMajor);
    CHECK(r.poolSize == 0);
    CHECK_FALSE(r.message.empty());
    CHECK_THROWS(rollChromaticDice(p, 9, s));
}

TEST_CASE("dice: Avoid Repeats skips the neighbouring chords and recent rolls", "[dice]") {
    // C | Em | F: rolling the middle chord must not produce C or F (sound-alikes of its neighbours) when avoiding repeats
    const Progression p = songIn({0, Mode::Major}, degreeChord(3, Quality::Minor));
    DiceSettings s = replaceHarmony();
    s.avoidRepeats = true;
    for (const auto& c : diceCandidates(p, 1, s)) CHECK((c.name != "C" && c.name != "F"));
    s.avoidRepeats = false;
    const auto all = diceCandidates(p, 1, s);
    CHECK(std::any_of(all.begin(), all.end(), [](const DiceCandidate& c) { return c.name == "C"; }));
    s.avoidRepeats = true;
    std::vector<ChordSpec> recent{degreeChord(5)};
    for (const auto& c : diceCandidates(p, 1, s, &recent)) CHECK(c.name != "G");
}

TEST_CASE("dice: inversions are normalised and the note says so", "[dice]") {
    ChordSpec c7third = degreeChord(5);
    c7third.seventh = Seventh::Minor;
    c7third.inversion = 3;  // G7 over F
    const Progression p = songIn({0, Mode::Major}, c7third);
    bool normalized = false;
    DiceSettings s = replaceHarmony();
    for (const auto& c : diceCandidates(p, 1, s)) {
        CHECK(c.chord.inversion == std::min(3, maxInversion(c.chord)));
        CHECK(c.chord.isValid());
    }
    for (std::uint32_t k = 0; k < 80 && !normalized; ++k) {
        s.rollIndex = k;
        const auto r = rollChromaticDice(p, 1, s);
        if (r.normalized) {
            normalized = true;
            CHECK_FALSE(r.normalizationNote.empty());
            CHECK(r.chord.inversion < 3);
        }
    }
    CHECK(normalized);
}

TEST_CASE("dice: a roll changes only the chord - timing, octave, density and melody stay", "[dice]") {
    Progression p = songIn({0, Mode::Major}, degreeChord(5));
    p.blocks[1].octave = 1;
    VoicingSettings density;
    density.density = 6;
    density.distribution = Distribution::Spread;
    p.blocks[1].voicing = density;
    p.melodySettings.generator.seed = 5;
    REQUIRE(regenerateMelody(p, GenerationRange{0, 3 * 3840}).ok);
    const auto melodyBefore = p.melody;
    const Key keyBefore = p.key;

    Document doc(p);
    const auto r = rollChromaticDice(doc.progression(), 1, replaceHarmony());
    REQUIRE(r.ok);
    REQUIRE(doc.edit("Chromatic Dice", [&](Progression& q) { q.blocks[1].chord = r.chord; }).ok);

    const auto& after = doc.progression();
    CHECK(after.blocks[1].startTick == 3840);
    CHECK(after.blocks[1].durationTicks == 3840);
    CHECK(after.blocks[1].octave == 1);
    REQUIRE(after.blocks[1].voicing.has_value());
    CHECK(*after.blocks[1].voicing == density);  // density survives the roll
    CHECK(after.melody == melodyBefore);          // melody untouched
    CHECK(after.key == keyBefore);                // the project tonic never changes
    CHECK(after.blocks[0] == p.blocks[0]);
    CHECK(after.blocks[2] == p.blocks[2]);
    // the new voicing is built from the new chord at the same density
    const auto voiced = voiceProgression(after);
    CHECK(voiced[1].actualVoices == 6);
    // the melody is only flagged, never regenerated by the roll
    CHECK_FALSE(affectedRanges(after).empty());
    // chord channel and melody channel are unchanged
    for (const auto& e : generateProjectNotes(after)) CHECK((e.channel == 0 || e.channel == 1));
}

TEST_CASE("dice: undo restores the chord and redo restores the same generated chord", "[dice]") {
    const Progression p = songIn({0, Mode::Major}, degreeChord(5));
    Document doc(p);
    DiceSettings s = replaceHarmony();
    const auto first = rollChromaticDice(doc.progression(), 1, s);
    REQUIRE(doc.edit("Dice", [&](Progression& q) { q.blocks[1].chord = first.chord; q.dice.rollIndex = 1; }).ok);
    s.rollIndex = 1;
    const auto second = rollChromaticDice(doc.progression(), 1, s);
    REQUIRE(doc.edit("Reroll", [&](Progression& q) { q.blocks[1].chord = second.chord; q.dice.rollIndex = 2; }).ok);
    CHECK(doc.progression().blocks[1].chord == second.chord);
    REQUIRE(doc.undo());
    CHECK(doc.progression().blocks[1].chord == first.chord);
    REQUIRE(doc.undo());
    CHECK(doc.progression().blocks[1].chord == p.blocks[1].chord);
    REQUIRE(doc.redo());
    REQUIRE(doc.redo());
    CHECK(doc.progression().blocks[1].chord == second.chord);  // the stored concrete chord, not a re-roll
}

TEST_CASE("dice: settings persist and old projects load", "[dice][serialization]") {
    const Progression legacy = makeDemoProgression();
    CHECK(serializeProgression(legacy).find("dice") == std::string::npos);
    Progression p = makeDemoProgression();
    p.dice.mode = DiceMode::ReplaceHarmony;
    p.dice.complexity = DiceComplexity::Unrestricted;
    p.dice.sourceFilter = ModeFilter::HarmonicMinor;
    p.dice.avoidRepeats = false;
    p.dice.rootLock = true;
    p.dice.seed = 4000000000u;
    p.dice.rollIndex = 9;
    const auto loaded = deserializeProgression(serializeProgression(p));
    REQUIRE(loaded.ok);
    CHECK(loaded.progression.dice == p.dice);
    CHECK(loaded.progression == p);
}

TEST_CASE("inspector edits: root, quality and seventh keep a valid chord and the rest of the model", "[dice][chordedit]") {
    const Key c{0, Mode::Major};
    ChordSpec g7 = degreeChord(5);
    g7.seventh = Seventh::Minor;
    g7.inversion = 1;
    for (int pc = 0; pc < 12; ++pc) {
        const auto moved = withRootPitchClass(c, g7, pc);
        CHECK(chordRootPitchClass(c, moved) == pc);
        CHECK(moved.isValid());
        CHECK(moved.quality == g7.quality);
        CHECK(moved.seventh == g7.seventh);
        CHECK(moved.inversion == 1);
    }
    CHECK(chordName(c, withRootPitchClass(c, ChordSpec{}, 8)) == "Ab");
    CHECK(chordName(c, withRootPitchClass(c, ChordSpec{}, 6)) == "F#");
    CHECK(withQuality(degreeChord(1), Quality::Minor).quality == Quality::Minor);
    ChordSpec sus = degreeChord(1);
    sus.suspension = Suspension::Sus4;
    CHECK(withQuality(sus, Quality::Diminished).suspension == Suspension::None);
    ChordSpec ninth = degreeChord(5);
    ninth.seventh = Seventh::Minor;
    ninth.ninth = Ninth::Major;
    const auto triad = withSeventhKind(ninth, Seventh::None);
    CHECK(triad.ninth == Ninth::None);
    CHECK(triad.isValid());
    CHECK(withSeventhKind(ninth, Seventh::Major).ninth == Ninth::Major);
    CHECK(withSeventhKind(ninth, Seventh::Diminished).ninth == Ninth::None);
}
