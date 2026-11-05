// Ninth chords (milestone 5): intervals, diatonic ninths, names, Roman numerals, edits, state, export.
// Expected values are worked out by hand.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/ChordEdit.h>
#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>
#include <chordcore/RomanNumeral.h>
#include <chordcore/Serialization.h>
#include <chordcore/Theory.h>

#include "support/SmfParser.h"

using namespace chordcore;

namespace {
ChordSpec nine(int degree, Mode mode) {
    ChordSpec c = withNinthToggled(mode, diatonicChord(mode, degree));
    return c;
}
const Key kC{0, Mode::Major};
}  // namespace

TEST_CASE("ninth: diatonic ninths in major and minor", "[ninth]") {
    // major: I IV are maj9, ii vi are m9, V is 9 (all major ninths), iii and vii have a flat 9
    const Ninth major[7] = {Ninth::Major, Ninth::Major, Ninth::Minor, Ninth::Major, Ninth::Major, Ninth::Major, Ninth::Minor};
    // natural minor: ii and v and the dim have... root-to-next-scale-note: i 2, ii 1, III 2, iv 2, v 1, VI 2, VII 2
    const Ninth minor[7] = {Ninth::Major, Ninth::Minor, Ninth::Major, Ninth::Major, Ninth::Minor, Ninth::Major, Ninth::Major};
    for (int d = 1; d <= 7; ++d) {
        CHECK(diatonicNinth(Mode::Major, d) == major[d - 1]);
        CHECK(diatonicNinth(Mode::Minor, d) == minor[d - 1]);
    }
}

TEST_CASE("ninth: intervals and voicing", "[ninth]") {
    CHECK(chordIntervals(nine(1, Mode::Major)) == std::vector<int>{0, 4, 7, 11, 14});   // Cmaj9
    CHECK(chordIntervals(nine(5, Mode::Major)) == std::vector<int>{0, 4, 7, 10, 14});   // G9
    CHECK(chordIntervals(nine(2, Mode::Major)) == std::vector<int>{0, 3, 7, 10, 14});   // Dm9
    CHECK(chordIntervals(nine(3, Mode::Major)) == std::vector<int>{0, 3, 7, 10, 13});   // Em7b9
    CHECK(chordIntervals(nine(7, Mode::Major)) == std::vector<int>{0, 3, 6, 10, 13});   // Bm7b5b9
    // C major 9 with root base 48: C3 E3 G3 B3 D4
    CHECK(voiceChord(kC, nine(1, Mode::Major)) == std::vector<int>{48, 52, 55, 59, 62});
}

TEST_CASE("ninth: validity", "[ninth]") {
    ChordSpec c = nine(5, Mode::Major);
    CHECK(c.isValid());
    ChordSpec noSeventh = c; noSeventh.seventh = Seventh::None;
    CHECK_FALSE(noSeventh.isValid());
    ChordSpec inverted = c; inverted.inversion = 2;
    CHECK(inverted.isValid());
    inverted.inversion = 3;
    CHECK_FALSE(inverted.isValid());  // ninth chords stop at the second inversion
    ChordSpec dim7 = nine(7, Mode::Major); dim7.seventh = Seventh::Diminished;
    CHECK_FALSE(dim7.isValid());
}

TEST_CASE("ninth: chord names", "[ninth]") {
    CHECK(chordName(kC, nine(1, Mode::Major)) == "Cmaj9");
    CHECK(chordName(kC, nine(2, Mode::Major)) == "Dm9");
    CHECK(chordName(kC, nine(3, Mode::Major)) == "Em7b9");
    CHECK(chordName(kC, nine(4, Mode::Major)) == "Fmaj9");
    CHECK(chordName(kC, nine(5, Mode::Major)) == "G9");
    CHECK(chordName(kC, nine(6, Mode::Major)) == "Am9");
    CHECK(chordName(kC, nine(7, Mode::Major)) == "Bm7b5b9");
    ChordSpec sus = nine(5, Mode::Major); sus.suspension = Suspension::Sus4;
    CHECK(chordName(kC, sus) == "G9sus4");
    CHECK(chordName(Key{9, Mode::Minor}, nine(1, Mode::Minor)) == "Am9");
}

TEST_CASE("ninth: Roman numerals", "[ninth][roman]") {
    CHECK(*formatRomanNumeral(nine(5, Mode::Major)) == "V9");
    CHECK(*formatRomanNumeral(nine(1, Mode::Major)) == "Imaj9");
    CHECK(*formatRomanNumeral(nine(2, Mode::Major)) == "ii9");
    CHECK(*formatRomanNumeral(nine(3, Mode::Major)) == "iii7b9");
    CHECK(*formatRomanNumeral(nine(7, Mode::Major)) == "vii\xC3\xB8" "7b9");
    ChordSpec sus = nine(5, Mode::Major); sus.suspension = Suspension::Sus4;
    CHECK(*formatRomanNumeral(sus) == "V9sus4");

    // every valid ninth chord round-trips through text
    int checked = 0;
    for (int d = 1; d <= 7; ++d)
        for (auto q : {Quality::Major, Quality::Minor, Quality::Diminished, Quality::Augmented})
            for (auto sev : {Seventh::Minor, Seventh::Major})
                for (auto n : {Ninth::Minor, Ninth::Major})
                    for (auto sus : {Suspension::None, Suspension::Sus2, Suspension::Sus4})
                        for (int acc = -1; acc <= 1; ++acc) {
                            for (int inv = 0; inv <= 2; ++inv) {
                                ChordSpec c{d, acc, q, sev, inv, sus, n};
                                const auto text = formatRomanNumeral(c);
                                if (!text) continue;
                                const auto back = parseRomanNumeral(*text);
                                if (!back.ok) continue;  // forms the grammar cannot express (e.g. dim + major 7th + suspension)
                                CHECK(back.chord == c);
                                ++checked;
                            }
                        }
    CHECK(checked > 100);
    CHECK(parseRomanNumeral("V9").ok);
    CHECK(parseRomanNumeral("V9").chord.ninth == Ninth::Major);
    CHECK(parseRomanNumeral("iii7b9").chord.ninth == Ninth::Minor);
    CHECK_FALSE(parseRomanNumeral("V9x").ok);
    CHECK_FALSE(parseRomanNumeral("viio9").ok);  // fully diminished seventh + ninth is not supported
}

TEST_CASE("ninth: edit rules", "[ninth][edit]") {
    const auto c = diatonicChord(Mode::Major, 5);
    const auto on = withNinthToggled(Mode::Major, c);
    CHECK(on.seventh == Seventh::Minor);  // adds the diatonic seventh
    CHECK(on.ninth == Ninth::Major);
    CHECK(withNinthToggled(Mode::Major, on).ninth == Ninth::None);
    CHECK(withNinthToggled(Mode::Major, on).seventh == Seventh::Minor);  // leaves the seventh chord

    ChordSpec inverted = withSeventhToggled(Mode::Major, c); inverted.inversion = 2;
    CHECK(withNinthToggled(Mode::Major, inverted).inversion == 2);  // kept where the ninth chord allows it
    inverted.inversion = 3;
    CHECK(withNinthToggled(Mode::Major, inverted).inversion == 2);  // clamped
    CHECK(withNinthToggled(Mode::Major, inverted).isValid());

    CHECK(withSeventhToggled(Mode::Major, on).ninth == Ninth::None);   // 7th button on a 9th chord: plain 7th
    CHECK(withSeventhToggled(Mode::Major, on).seventh == Seventh::Minor);
    CHECK(asTriad(on).ninth == Ninth::None);
    CHECK(asTriad(on).seventh == Seventh::None);

    // changing degree retargets the ninth (V9 -> iii has a flat 9)
    CHECK(withDegree(Mode::Major, on, 3).ninth == Ninth::Minor);
    CHECK(withDegree(Mode::Major, on, 3).isValid());

    Progression p;
    p.blocks = {{nine(2, Mode::Major), 0, 3840}};  // ii9 in major: major ninth; in minor ii has a flat 9
    changeMode(p, Mode::Minor);
    CHECK(p.blocks[0].chord.ninth == diatonicNinth(Mode::Minor, 2));
    CHECK(p.blocks[0].chord.isValid());
    CHECK(p.validate().empty());
}

TEST_CASE("ninth: state round trip, and old files without it still load", "[ninth][state]") {
    Progression p;
    p.blocks = {{nine(5, Mode::Major), 0, 1920}, {nine(3, Mode::Major), 1920, 1920}, {diatonicChord(Mode::Major, 1), 3840, 960}};
    const auto text = serializeProgression(p);
    CHECK(text.find("\"ninth\":\"major\"") != std::string::npos);
    CHECK(text.find("\"ninth\":\"minor\"") != std::string::npos);
    const auto back = deserializeProgression(text);
    REQUIRE(back.ok);
    CHECK(back.progression.blocks == p.blocks);

    Progression plain;
    plain.blocks = {{diatonicChord(Mode::Major, 1), 0, 3840}};
    CHECK(serializeProgression(plain).find("ninth") == std::string::npos);  // written only when set
    CHECK(deserializeProgression(serializeProgression(plain)).ok);

    auto bad = text;
    bad.replace(bad.find("\"ninth\":\"major\""), 15, "\"ninth\":\"nope\"");
    CHECK_FALSE(deserializeProgression(bad).ok);
}

TEST_CASE("ninth: generation and MIDI export carry all five notes", "[ninth][export]") {
    Progression p;
    p.blocks = {{nine(5, Mode::Major), 1440, 960}};
    const auto notes = generateNotes(p);
    REQUIRE(notes.size() == 5);
    for (const auto& n : notes) { CHECK(n.startTick == 1440); CHECK(n.startTick + n.lengthTicks == 2400); }
    const auto file = smf::parse(exportProgressionToMidi(p));
    CHECK(smf::extractNotes(file.tracks[0]).size() == 5);
}
