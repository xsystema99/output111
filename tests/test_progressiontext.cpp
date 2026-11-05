// Chord text ("I V vi:2 - IV") and the Roman numeral formatter (the parser's inverse).

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Progression.h>
#include <chordcore/ProgressionText.h>
#include <chordcore/RomanNumeral.h>

#include <string>
#include <vector>

using namespace chordcore;

namespace {
const TimeSignature k44{4, 4};

std::vector<ChordBlock> parseOk(const std::string& text, const TimeSignature& ts = k44) {
    const auto r = parseProgressionText(text, ts);
    INFO("text: '" << text << "' error: " << r.error);
    REQUIRE(r.ok);
    return r.blocks;
}
}  // namespace

// ------------------------------------------------------------------------------------------------
// Roman numeral formatter
// ------------------------------------------------------------------------------------------------

TEST_CASE("formatRomanNumeral: canonical symbols", "[roman][format]") {
    struct Case { ChordSpec chord; const char* text; };
    const Case cases[] = {
        {{1, 0, Quality::Major, Seventh::None, 0, Suspension::None}, "I"},
        {{2, 0, Quality::Minor, Seventh::None, 0, Suspension::None}, "ii"},
        {{6, 0, Quality::Minor, Seventh::None, 1, Suspension::None}, "vi6"},
        {{1, 0, Quality::Major, Seventh::None, 2, Suspension::None}, "I64"},
        {{5, 0, Quality::Major, Seventh::Minor, 0, Suspension::None}, "V7"},
        {{5, 0, Quality::Major, Seventh::Minor, 1, Suspension::None}, "V65"},
        {{5, 0, Quality::Major, Seventh::Minor, 2, Suspension::None}, "V43"},
        {{5, 0, Quality::Major, Seventh::Minor, 3, Suspension::None}, "V42"},
        {{2, 0, Quality::Minor, Seventh::Minor, 0, Suspension::None}, "ii7"},
        {{1, 0, Quality::Major, Seventh::Major, 0, Suspension::None}, "Imaj7"},
        {{1, 0, Quality::Minor, Seventh::Major, 0, Suspension::None}, "imaj7"},
        {{7, 0, Quality::Diminished, Seventh::None, 0, Suspension::None}, "viio"},
        {{7, 0, Quality::Diminished, Seventh::Diminished, 0, Suspension::None}, "viio7"},
        {{7, 0, Quality::Diminished, Seventh::Minor, 0, Suspension::None}, "vii\xC3\xB8" "7"},
        {{7, 0, Quality::Diminished, Seventh::Minor, 1, Suspension::None}, "vii\xC3\xB8" "65"},
        {{3, 0, Quality::Augmented, Seventh::None, 0, Suspension::None}, "III+"},
        {{3, 0, Quality::Augmented, Seventh::Minor, 0, Suspension::None}, "III+7"},
        {{7, -1, Quality::Major, Seventh::None, 0, Suspension::None}, "bVII"},
        {{4, +1, Quality::Diminished, Seventh::None, 0, Suspension::None}, "#ivo"},
        {{5, 0, Quality::Major, Seventh::None, 0, Suspension::Sus4}, "Vsus4"},
        {{5, 0, Quality::Major, Seventh::None, 0, Suspension::Sus2}, "Vsus2"},
        {{5, 0, Quality::Major, Seventh::Minor, 0, Suspension::Sus4}, "V7sus4"},
        {{1, 0, Quality::Major, Seventh::None, 2, Suspension::Sus4}, "I64sus4"},
        {{6, 0, Quality::Minor, Seventh::None, 0, Suspension::Sus4}, "visus4"},
    };
    for (const auto& c : cases) {
        INFO("expected " << c.text);
        const auto s = formatRomanNumeral(c.chord);
        REQUIRE(s.has_value());
        CHECK(*s == c.text);
    }
}

TEST_CASE("formatRomanNumeral: chords the grammar cannot express", "[roman][format]") {
    auto none = [](ChordSpec c) { CHECK_FALSE(formatRomanNumeral(c).has_value()); };
    none({1, 0, Quality::Diminished, Seventh::None, 0, Suspension::Sus4});   // sus replaces the third
    none({1, 0, Quality::Augmented, Seventh::None, 0, Suspension::Sus2});
    none({1, 0, Quality::Major, Seventh::Diminished, 0, Suspension::None});  // dim 7th needs a dim triad
    none({1, 0, Quality::Minor, Seventh::Diminished, 0, Suspension::None});
    none({1, 0, Quality::Augmented, Seventh::Diminished, 0, Suspension::None});
    none({1, 0, Quality::Major, Seventh::Major, 1, Suspension::None});       // inverted maj7
    none({0, 0, Quality::Major, Seventh::None, 0, Suspension::None});        // invalid chord
    none({1, 0, Quality::Major, Seventh::None, 3, Suspension::None});
}

TEST_CASE("formatRomanNumeral and parseRomanNumeral are exact inverses (exhaustive)", "[roman][format]") {
    int expressible = 0, total = 0;
    for (int degree = 1; degree <= 7; ++degree)
    for (int accidental = -1; accidental <= 1; ++accidental)
    for (int q = 0; q < 4; ++q)
    for (int s = 0; s < 4; ++s)
    for (int inversion = 0; inversion <= 3; ++inversion)
    for (int sus = 0; sus < 3; ++sus) {
        const ChordSpec chord{degree, accidental, static_cast<Quality>(q), static_cast<Seventh>(s), inversion,
                              static_cast<Suspension>(sus)};
        if (!chord.isValid()) continue;
        ++total;
        const auto text = formatRomanNumeral(chord);
        if (!text) continue;
        ++expressible;
        const auto back = parseRomanNumeral(*text);
        INFO("symbol: " << *text);
        REQUIRE(back.ok);
        // A suspended chord has no third, so its quality is not distinguishable: it reads back as the
        // numeral's case (the formatter only ever emits major/minor there).
        REQUIRE(back.chord == chord);
    }
    CHECK(total > 500);
    CHECK(expressible > 300);  // the check is not vacuous
}

TEST_CASE("parse -> format -> parse is stable for a corpus of symbols", "[roman][format]") {
    const char* corpus[] = {"I", "ii", "iii", "IV", "V", "vi", "viio", "vii", "I6", "I64", "V7", "V65", "V43", "V42", "V2",
                            "Imaj7", "IVM7", "ii7", "bVII", "#ivo", "III+", "viio7", "vii\xC3\xB8", "vii\xC3\xB8" "65",
                            "Vsus4", "Vsus", "Vsus2", "V7sus4", "I64sus4", "visus4", "bVIIsus2", "\xE2\x99\xAD" "VI"};
    for (const char* symbol : corpus) {
        INFO("symbol " << symbol);
        const auto first = parseRomanNumeral(symbol);
        REQUIRE(first.ok);
        const auto text = formatRomanNumeral(first.chord);
        REQUIRE(text.has_value());  // everything the parser can produce is expressible
        const auto second = parseRomanNumeral(*text);
        REQUIRE(second.ok);
        CHECK(second.chord == first.chord);
    }
}

// ------------------------------------------------------------------------------------------------
// Chord text
// ------------------------------------------------------------------------------------------------

TEST_CASE("parseProgressionText: the milestone progression", "[text]") {
    const auto blocks = parseOk("I V vi IV");
    CHECK(blocks == makeDemoProgression().blocks);
}

TEST_CASE("parseProgressionText: lengths, rests and separators", "[text]") {
    SECTION("explicit lengths in bars") {
        const auto b = parseOk("I:2 V");
        REQUIRE(b.size() == 2);
        CHECK(b[0].startTick == 0);
        CHECK(b[0].durationTicks == 7680);
        CHECK(b[1].startTick == 7680);
        CHECK(b[1].durationTicks == 3840);
    }
    SECTION("fractions of a bar") {
        const auto b = parseOk("I:0.5 V:0.5 vi:1.5");
        REQUIRE(b.size() == 3);
        CHECK(b[0].durationTicks == 1920);
        CHECK(b[1].startTick == 1920);
        CHECK(b[2].startTick == 3840);
        CHECK(b[2].durationTicks == 5760);
    }
    SECTION("a rest is a gap") {
        const auto b = parseOk("I - V");
        REQUIRE(b.size() == 2);
        CHECK(b[1].startTick == 7680);
    }
    SECTION("a leading, sized rest") {
        const auto b = parseOk("-:0.5 I");
        REQUIRE(b.size() == 1);
        CHECK(b[0].startTick == 1920);
    }
    SECTION("trailing rests add nothing") { CHECK(parseOk("I V -:4").size() == 2); }
    SECTION("separators: whitespace, commas, bars") {
        const auto b = parseOk("  I,V | vi\t IV\n");
        CHECK(b == makeDemoProgression().blocks);
    }
    SECTION("empty text is an empty progression") {
        CHECK(parseOk("").empty());
        CHECK(parseOk("  , | ").empty());
    }
    SECTION("a bar depends on the time signature") {
        const auto b = parseOk("I V", TimeSignature{3, 4});
        CHECK(b[0].durationTicks == 2880);
        CHECK(b[1].startTick == 2880);
        const auto c = parseOk("I V:2", TimeSignature{6, 8});
        CHECK(c[1].durationTicks == 5760);
    }
    SECTION("suspensions and inversions in a line") {
        const auto b = parseOk("Isus2 Vsus4 V7sus4:2 I64");
        REQUIRE(b.size() == 4);
        CHECK(b[0].chord.suspension == Suspension::Sus2);
        CHECK(b[1].chord.suspension == Suspension::Sus4);
        CHECK(b[2].chord.seventh == Seventh::Minor);
        CHECK(b[2].durationTicks == 7680);
        CHECK(b[3].chord.inversion == 2);
    }
}

TEST_CASE("parseProgressionText: rejects bad input and names the token", "[text][errors]") {
    const char* bad[] = {"I X V", "I VIII", "I:0", "I:-1", "I:abc", "I:", "I:100", "I:1e999", "I::2", "-:0", "-:x", "I V15"};
    for (const char* text : bad) {
        INFO("text: " << text);
        const auto r = parseProgressionText(text, k44);
        CHECK_FALSE(r.ok);
        CHECK_FALSE(r.error.empty());
        CHECK(r.blocks.empty());
    }
    const auto r = parseProgressionText("I X V", k44);
    CHECK(r.error.find("'X'") != std::string::npos);

    CHECK_FALSE(parseProgressionText("I", TimeSignature{4, 3}).ok);

    std::string many;
    for (int i = 0; i < 300; ++i) many += "I ";
    CHECK_FALSE(parseProgressionText(many, k44).ok);
}

TEST_CASE("formatProgressionText: demo and gaps", "[text]") {
    CHECK(formatProgressionText(makeDemoProgression()) == "I V vi IV");
    CHECK(formatProgressionText(Progression{}).empty());

    Progression p;
    p.blocks = {
        ChordBlock{ChordSpec{1, 0, Quality::Major, Seventh::None, 0, Suspension::None}, 0, 7680},
        ChordBlock{ChordSpec{5, 0, Quality::Major, Seventh::Minor, 0, Suspension::Sus4}, 11520, 1920},
        ChordBlock{ChordSpec{6, 0, Quality::Minor, Seventh::None, 0, Suspension::None}, 13440, 3840},
    };
    CHECK(formatProgressionText(p) == "I:2 - V7sus4:0.5 vi");

    Progression lead;
    lead.blocks = {ChordBlock{ChordSpec{4, 0, Quality::Major, Seventh::None, 0, Suspension::None}, 1920, 3840}};
    CHECK(formatProgressionText(lead) == "-:0.5 IV");
}

TEST_CASE("formatProgressionText: unexpressible chords show as '?'", "[text]") {
    Progression p;
    p.blocks = {ChordBlock{ChordSpec{1, 0, Quality::Major, Seventh::Diminished, 0, Suspension::None}, 0, 3840}};
    CHECK(formatProgressionText(p) == "?");
    CHECK_FALSE(parseProgressionText("?", k44).ok);
}

TEST_CASE("text round trip: parse(format(parse(t))) == parse(t)", "[text]") {
    const char* texts[] = {"I V vi IV", "I:2 V:0.5 vi:0.5 IV", "- I - V", "-:0.5 Vsus4:0.5 V:1", "bVII IV I", "I:3",
                           "ii7 V7 Imaj7:2", "vii\xC3\xB8" "7 III+ viio7", "I:0.25 V:0.75"};
    for (const char* t : texts) {
        INFO("text " << t);
        for (const auto& ts : {TimeSignature{4, 4}, TimeSignature{3, 4}, TimeSignature{6, 8}}) {
            const auto first = parseOk(t, ts);
            Progression p;
            p.timeSignature = ts;
            p.blocks = first;
            const auto second = parseOk(formatProgressionText(p), ts);
            CHECK(second == first);
        }
    }
}

TEST_CASE("progression text: octaves, inversions and ninths survive copy and paste", "[progressiontext][octave]") {
    const TimeSignature ts{4, 4};
    const auto parsed = parseProgressionText("I@+1 V9(1):2@-2 - vi65:0.5@+2", ts);
    REQUIRE(parsed.ok);
    REQUIRE(parsed.blocks.size() == 3);
    CHECK(parsed.blocks[0].octave == 1);
    CHECK(parsed.blocks[1].octave == -2);
    CHECK(parsed.blocks[1].chord.ninth == Ninth::Major);
    CHECK(parsed.blocks[1].chord.inversion == 1);
    CHECK(parsed.blocks[2].octave == 2);
    CHECK(parsed.blocks[2].startTick == 3840 * 4);

    Progression p;
    p.blocks = parsed.blocks;
    const auto text = formatProgressionText(p);
    CHECK(text == "I@+1 V9(1):2@-2 - vi65:0.5@+2");
    const auto again = parseProgressionText(text, ts);
    REQUIRE(again.ok);
    CHECK(again.blocks == p.blocks);

    for (const char* bad : {"I@3", "I@-3", "I@", "I@+", "I@x", "I@+11"}) CHECK_FALSE(parseProgressionText(bad, ts).ok);
    CHECK(parseProgressionText("I@0", ts).ok);
}
