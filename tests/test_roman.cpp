// Roman numeral parser: text -> ChordSpec, and text -> pitches through the theory layer.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/RomanNumeral.h>
#include <chordcore/Theory.h>

#include <string>
#include <vector>

using namespace chordcore;

namespace {

struct Case {
    const char* text;
    ChordSpec expected;
};

ChordSpec spec(int degree, int accidental, Quality q, Seventh s = Seventh::None, int inversion = 0,
               Suspension sus = Suspension::None) {
    return ChordSpec{degree, accidental, q, s, inversion, sus};
}

void checkParses(const Case& c) {
    INFO("symbol: " << c.text);
    const auto r = parseRomanNumeral(c.text);
    REQUIRE(r.ok);
    CHECK(r.error.empty());
    CHECK(r.chord == c.expected);
}

}  // namespace

TEST_CASE("parseRomanNumeral: triads", "[roman]") {
    const Case cases[] = {
        {"I", spec(1, 0, Quality::Major)},   {"ii", spec(2, 0, Quality::Minor)},
        {"iii", spec(3, 0, Quality::Minor)}, {"IV", spec(4, 0, Quality::Major)},
        {"V", spec(5, 0, Quality::Major)},   {"vi", spec(6, 0, Quality::Minor)},
        {"VII", spec(7, 0, Quality::Major)}, {"vii", spec(7, 0, Quality::Minor)},
        {"i", spec(1, 0, Quality::Minor)},   {"III", spec(3, 0, Quality::Major)},
        {"VI", spec(6, 0, Quality::Major)},
    };
    for (const auto& c : cases) checkParses(c);
}

TEST_CASE("parseRomanNumeral: diminished and augmented", "[roman]") {
    const Case cases[] = {
        {"viio", spec(7, 0, Quality::Diminished)},
        {"vii\xC2\xB0", spec(7, 0, Quality::Diminished)},  // vii°
        {"iio", spec(2, 0, Quality::Diminished)},
        {"III+", spec(3, 0, Quality::Augmented)},
    };
    for (const auto& c : cases) checkParses(c);
}

TEST_CASE("parseRomanNumeral: triad inversions", "[roman]") {
    const Case cases[] = {
        {"I6", spec(1, 0, Quality::Major, Seventh::None, 1)},
        {"I64", spec(1, 0, Quality::Major, Seventh::None, 2)},
        {"vi6", spec(6, 0, Quality::Minor, Seventh::None, 1)},
        {"i64", spec(1, 0, Quality::Minor, Seventh::None, 2)},
    };
    for (const auto& c : cases) checkParses(c);
}

TEST_CASE("parseRomanNumeral: seventh chords and their inversions", "[roman]") {
    const Case cases[] = {
        {"V7", spec(5, 0, Quality::Major, Seventh::Minor, 0)},
        {"V65", spec(5, 0, Quality::Major, Seventh::Minor, 1)},
        {"V43", spec(5, 0, Quality::Major, Seventh::Minor, 2)},
        {"V42", spec(5, 0, Quality::Major, Seventh::Minor, 3)},
        {"V2", spec(5, 0, Quality::Major, Seventh::Minor, 3)},
        {"ii7", spec(2, 0, Quality::Minor, Seventh::Minor, 0)},
        {"ii65", spec(2, 0, Quality::Minor, Seventh::Minor, 1)},
        {"Imaj7", spec(1, 0, Quality::Major, Seventh::Major, 0)},
        {"IVM7", spec(4, 0, Quality::Major, Seventh::Major, 0)},
        {"vii\xC3\xB8", spec(7, 0, Quality::Diminished, Seventh::Minor, 0)},       // viiø
        {"vii\xC3\xB8" "7", spec(7, 0, Quality::Diminished, Seventh::Minor, 0)},   // viiø7
        {"vii\xC3\xB8" "65", spec(7, 0, Quality::Diminished, Seventh::Minor, 1)},
        {"viio7", spec(7, 0, Quality::Diminished, Seventh::Diminished, 0)},
        {"vii\xC2\xB0" "65", spec(7, 0, Quality::Diminished, Seventh::Diminished, 1)},
    };
    for (const auto& c : cases) checkParses(c);
}

TEST_CASE("parseRomanNumeral: accidentals", "[roman]") {
    const Case cases[] = {
        {"bVII", spec(7, -1, Quality::Major)},
        {"bIII", spec(3, -1, Quality::Major)},
        {"\xE2\x99\xAD" "VI", spec(6, -1, Quality::Major)},  // flat sign
        {"#iv", spec(4, +1, Quality::Minor)},
        {"#iv" "o", spec(4, +1, Quality::Diminished)},
        {"\xE2\x99\xAF" "iv\xC2\xB0", spec(4, +1, Quality::Diminished)},  // sharp sign
        {"bVII7", spec(7, -1, Quality::Major, Seventh::Minor)},
    };
    for (const auto& c : cases) checkParses(c);
}

TEST_CASE("parseRomanNumeral: suspensions", "[roman][sus]") {
    const Case cases[] = {
        {"Vsus4", spec(5, 0, Quality::Major, Seventh::None, 0, Suspension::Sus4)},
        {"Vsus", spec(5, 0, Quality::Major, Seventh::None, 0, Suspension::Sus4)},
        {"Vsus2", spec(5, 0, Quality::Major, Seventh::None, 0, Suspension::Sus2)},
        {"Isus2", spec(1, 0, Quality::Major, Seventh::None, 0, Suspension::Sus2)},
        {"visus4", spec(6, 0, Quality::Minor, Seventh::None, 0, Suspension::Sus4)},
        {"V7sus4", spec(5, 0, Quality::Major, Seventh::Minor, 0, Suspension::Sus4)},
        {"Imaj7sus2", spec(1, 0, Quality::Major, Seventh::Major, 0, Suspension::Sus2)},
        {"I64sus4", spec(1, 0, Quality::Major, Seventh::None, 2, Suspension::Sus4)},
        {"V65sus4", spec(5, 0, Quality::Major, Seventh::Minor, 1, Suspension::Sus4)},
        {"bVIIsus2", spec(7, -1, Quality::Major, Seventh::None, 0, Suspension::Sus2)},
    };
    for (const auto& c : cases) checkParses(c);
}

TEST_CASE("parseRomanNumeral: rejects malformed or unsupported input with a message", "[roman]") {
    const char* bad[] = {
        "",         "   ",    "x",        "b",      "bb",       "VIII",   "iV",
        "Io",       "i+",     "V11",      "V13",    "Vsus3",    "I7 ",    " I",
        "Vsus44",   "Vsus4sus2", "viio" "sus4", "I+sus4", "vii\xC3\xB8" "sus4", "sus4",
        "vii\xC3\xB8" "6",  "vii\xC3\xB8" "maj7",   "I1",       "V5",
    };
    for (const char* text : bad) {
        INFO("symbol: '" << text << "'");
        const auto r = parseRomanNumeral(text);
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.error.empty());
    }
}

TEST_CASE("parseRomanNumeral + voiceChord: symbols produce the expected pitches", "[roman][pitch]") {
    const Key cMajor{0, Mode::Major};
    auto pitches = [&](const char* sym) {
        const auto r = parseRomanNumeral(sym);
        REQUIRE(r.ok);
        return voiceChord(cMajor, r.chord);
    };
    using P = std::vector<int>;
    CHECK(pitches("I") == P{48, 52, 55});
    CHECK(pitches("V") == P{55, 59, 62});
    CHECK(pitches("vi") == P{57, 60, 64});
    CHECK(pitches("IV") == P{53, 57, 60});
    CHECK(pitches("V65") == P{59, 62, 65, 67});
    CHECK(pitches("I64") == P{55, 60, 64});
    CHECK(pitches("bVII") == P{58, 62, 65});
    CHECK(pitches("vii\xC3\xB8" "7") == P{59, 62, 65, 69});
    CHECK(pitches("Vsus4") == P{55, 60, 62});
    CHECK(pitches("Vsus2") == P{55, 57, 62});
    CHECK(pitches("V7sus4") == P{55, 60, 62, 65});
}
