// Inversion selection: limits, voicing, names, numerals and edit rules. Expected values are worked out by hand.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/ChordEdit.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>
#include <chordcore/RomanNumeral.h>
#include <chordcore/Serialization.h>
#include <chordcore/Theory.h>

#include <algorithm>

using namespace chordcore;

namespace {
const Key kC{0, Mode::Major};
using P = std::vector<int>;
}  // namespace

TEST_CASE("inversion: limits per chord type", "[inversion]") {
    CHECK(maxInversion(diatonicChord(Mode::Major, 1)) == 2);
    CHECK(maxInversion(withSeventhToggled(Mode::Major, diatonicChord(Mode::Major, 1))) == 3);
    CHECK(maxInversion(withNinthToggled(Mode::Major, diatonicChord(Mode::Major, 1))) == 2);
    const auto triad = diatonicChord(Mode::Major, 1);
    CHECK(withInversion(triad, 5).inversion == 2);
    CHECK(withInversion(triad, -3).inversion == 0);
    CHECK(withInversion(triad, 1).inversion == 1);
}

TEST_CASE("inversion: voicing of C major triad, 7th and 9th", "[inversion]") {
    const auto triad = diatonicChord(Mode::Major, 1);
    CHECK(voiceChord(kC, withInversion(triad, 0)) == P{48, 52, 55});
    CHECK(voiceChord(kC, withInversion(triad, 1)) == P{52, 55, 60});
    CHECK(voiceChord(kC, withInversion(triad, 2)) == P{55, 60, 64});

    const auto seventh = withSeventhToggled(Mode::Major, triad);  // Cmaj7
    CHECK(voiceChord(kC, withInversion(seventh, 3)) == P{59, 60, 64, 67});

    const auto ninth = withNinthToggled(Mode::Major, triad);  // Cmaj9: C E G B D
    CHECK(voiceChord(kC, withInversion(ninth, 1)) == P{52, 55, 59, 60, 62});  // sorted: the root moved above the 7th
    CHECK(voiceChord(kC, withInversion(ninth, 2)) == P{55, 59, 60, 62, 64});
}

TEST_CASE("inversion: same pitch classes in every inversion, bass is the expected chord tone", "[inversion]") {
    for (int d = 1; d <= 7; ++d)
        for (bool seventh : {false, true})
            for (bool ninth : {false, true}) {
                auto c = diatonicChord(Mode::Major, d);
                if (seventh) c = withSeventhToggled(Mode::Major, c);
                if (ninth) c = withNinthToggled(Mode::Major, diatonicChord(Mode::Major, d));
                const auto root = voiceChord(kC, c);
                std::vector<int> rootClasses;
                for (int p : root) rootClasses.push_back(p % 12);
                std::sort(rootClasses.begin(), rootClasses.end());
                for (int inv = 1; inv <= maxInversion(c); ++inv) {
                    const auto v = voiceChord(kC, withInversion(c, inv));
                    REQUIRE(v.size() == root.size());
                    REQUIRE(std::is_sorted(v.begin(), v.end()));
                    std::vector<int> classes;
                    for (int p : v) classes.push_back(p % 12);
                    std::sort(classes.begin(), classes.end());
                    CHECK(classes == rootClasses);
                    const int bass = (chordRootPitchClass(kC, c) + chordIntervals(c)[static_cast<std::size_t>(inv)]) % 12;
                    CHECK(v.front() % 12 == bass);
                }
            }
}

TEST_CASE("inversion: names show the bass note", "[inversion]") {
    const auto triad = diatonicChord(Mode::Major, 1);
    CHECK(chordName(kC, withInversion(triad, 1)) == "C/E");
    CHECK(chordName(kC, withInversion(triad, 2)) == "C/G");
    const auto g9 = withNinthToggled(Mode::Major, diatonicChord(Mode::Major, 5));
    CHECK(chordName(kC, withInversion(g9, 1)) == "G9/B");
    CHECK(chordName(kC, withInversion(g9, 2)) == "G9/D");
}

TEST_CASE("inversion: Roman numerals, including ninth chords", "[inversion][roman]") {
    const auto v9 = withNinthToggled(Mode::Major, diatonicChord(Mode::Major, 5));
    CHECK(*formatRomanNumeral(withInversion(v9, 1)) == "V9(1)");
    CHECK(*formatRomanNumeral(withInversion(v9, 2)) == "V9(2)");
    for (int inv = 0; inv <= 2; ++inv) {
        const auto r = parseRomanNumeral(*formatRomanNumeral(withInversion(v9, inv)));
        REQUIRE(r.ok);
        CHECK(r.chord == withInversion(v9, inv));
    }
    CHECK(parseRomanNumeral("Imaj9(2)").chord.inversion == 2);
    CHECK_FALSE(parseRomanNumeral("V7(1)").ok);   // marker only for ninth chords
    CHECK_FALSE(parseRomanNumeral("V9(3)").ok);   // beyond the ninth chord limit
    CHECK_FALSE(parseRomanNumeral("V(1)").ok);
    CHECK(*formatRomanNumeral(withInversion(withSeventhToggled(Mode::Major, diatonicChord(Mode::Major, 5)), 1)) == "V65");  // unchanged
}

TEST_CASE("inversion: kept or clamped by other edits", "[inversion][edit]") {
    auto c = withInversion(diatonicChord(Mode::Major, 1), 2);
    CHECK(withDegree(Mode::Major, c, 5).inversion == 2);
    auto seventh = withInversion(withSeventhToggled(Mode::Major, diatonicChord(Mode::Major, 1)), 3);
    CHECK(asTriad(seventh).inversion == 2);

    Progression p;
    p.blocks = {{withInversion(diatonicChord(Mode::Major, 1), 1), 0, 3840}};
    const auto back = deserializeProgression(serializeProgression(p));
    REQUIRE(back.ok);
    CHECK(back.progression.blocks[0].chord.inversion == 1);
    CHECK(generateNotes(back.progression) == generateNotes(p));
}

TEST_CASE("inversion: every inversion fits MIDI range at the extreme octave settings", "[inversion][range]") {
    for (int d = 1; d <= 7; ++d)
        for (int key = 0; key < 12; ++key)
            for (bool ninth : {false, true}) {
                auto c = ninth ? withNinthToggled(Mode::Major, diatonicChord(Mode::Major, d)) : withSeventhToggled(Mode::Major, diatonicChord(Mode::Major, d));
                for (int inv = 0; inv <= maxInversion(c); ++inv)
                    for (int oct : {-2, 2}) {
                        Progression p;
                        p.key.tonic = key;
                        p.octave = oct;
                        p.blocks = {{withInversion(c, inv), 0, 960}};
                        p.blocks[0].octave = oct;
                        REQUIRE(p.validate().empty());
                        CHECK_NOTHROW(generateNotes(p));
                    }
            }
}
