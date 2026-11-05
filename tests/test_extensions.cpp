// 11th, 13th and altered (b9 #9 #11 b13) chords: voicing, names, numerals, edits, state, range rule.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/ChordEdit.h>
#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>
#include <chordcore/ProgressionText.h>
#include <chordcore/RomanNumeral.h>
#include <chordcore/Serialization.h>
#include <chordcore/Theory.h>

#include "support/SmfParser.h"

using namespace chordcore;

namespace {
const Key kC{0, Mode::Major};
ChordSpec triad(int d, Mode m = Mode::Major) { return diatonicChord(m, d); }
using IV = std::vector<int>;
}  // namespace

TEST_CASE("extensions: diatonic 11ths and 13ths", "[ext]") {
    // major: only IV has the sharp 11 (F-B); only vi... 13th: iii (E-C) has the flat 13, vi? A-F# major 13; ii: D-B major 13
    const Eleventh e[7] = {Eleventh::Perfect, Eleventh::Perfect, Eleventh::Perfect, Eleventh::Sharp, Eleventh::Perfect, Eleventh::Perfect, Eleventh::Perfect};
    const Thirteenth t[7] = {Thirteenth::Major, Thirteenth::Major, Thirteenth::Minor, Thirteenth::Major, Thirteenth::Major, Thirteenth::Minor, Thirteenth::Minor};
    for (int d = 1; d <= 7; ++d) {
        CHECK(diatonicEleventh(Mode::Major, d) == e[d - 1]);
        CHECK(diatonicThirteenth(Mode::Major, d) == t[d - 1]);
    }
    CHECK(diatonicEleventh(Mode::Minor, 2) == Eleventh::Perfect);  // ii (B dim): B-E
    CHECK(diatonicEleventh(Mode::Minor, 6) == Eleventh::Sharp);    // VI (F): F-B natural minor has B? A natural minor: F->B = 6 semitones
}

TEST_CASE("extensions: intervals for stacked chords", "[ext]") {
    auto g = withNinthToggled(Mode::Major, triad(5));  // G9
    CHECK(chordIntervals(withThirteenthToggled(Mode::Major, g)) == IV{0, 4, 7, 10, 14, 21});          // G13 (no 11)
    CHECK(chordIntervals(withEleventhToggled(Mode::Major, g)) == IV{0, 4, 7, 10, 14, 17});            // G11
    CHECK(chordIntervals(withAlterationToggled(Mode::Major, g, Alteration::SharpNine)) == IV{0, 4, 7, 10, 15});
    CHECK(chordIntervals(withAlterationToggled(Mode::Major, g, Alteration::FlatNine)) == IV{0, 4, 7, 10, 13});
    CHECK(chordIntervals(withAlterationToggled(Mode::Major, g, Alteration::SharpEleven)) == IV{0, 4, 7, 10, 14, 18});
    CHECK(chordIntervals(withAlterationToggled(Mode::Major, g, Alteration::FlatThirteen)) == IV{0, 4, 7, 10, 14, 20});
    // G13 voiced from root base 48: G3 B3 D4 F4 A4 E5
    CHECK(voiceChord(kC, withThirteenthToggled(Mode::Major, g)) == IV{55, 59, 62, 65, 69, 76});
}

TEST_CASE("extensions: names", "[ext][name]") {
    auto g9 = withNinthToggled(Mode::Major, triad(5));
    CHECK(chordName(kC, withThirteenthToggled(Mode::Major, g9)) == "G13");
    CHECK(chordName(kC, withEleventhToggled(Mode::Major, g9)) == "G11");
    CHECK(chordName(kC, withAlterationToggled(Mode::Major, g9, Alteration::SharpNine)) == "G7#9");
    CHECK(chordName(kC, withAlterationToggled(Mode::Major, g9, Alteration::FlatNine)) == "G7b9");
    CHECK(chordName(kC, withAlterationToggled(Mode::Major, g9, Alteration::SharpEleven)) == "G9#11");
    CHECK(chordName(kC, withAlterationToggled(Mode::Major, g9, Alteration::FlatThirteen)) == "G9b13");
    auto g13 = withThirteenthToggled(Mode::Major, g9);
    CHECK(chordName(kC, withAlterationToggled(Mode::Major, g13, Alteration::SharpEleven)) == "G13#11");
    CHECK(chordName(kC, withAlterationToggled(Mode::Major, g13, Alteration::FlatNine)) == "G13b9");
    CHECK(chordName(kC, withEleventhToggled(Mode::Major, withNinthToggled(Mode::Major, triad(2)))) == "Dm11");
    CHECK(chordName(kC, withThirteenthToggled(Mode::Major, withNinthToggled(Mode::Major, triad(1)))) == "Cmaj13");
    CHECK(chordName(kC, withThirteenthToggled(Mode::Major, withNinthToggled(Mode::Major, triad(7)))) == "Bm7b5b9b13");
}

TEST_CASE("extensions: validity rules", "[ext]") {
    auto g9 = withNinthToggled(Mode::Major, triad(5));
    ChordSpec c = g9;
    c.eleventh = Eleventh::Perfect;
    CHECK(c.isValid());
    c.thirteenth = Thirteenth::Major;
    CHECK_FALSE(c.isValid());  // natural 11 and natural 13 do not combine
    c.eleventh = Eleventh::Sharp;
    CHECK(c.isValid());        // 13#11 does
    ChordSpec noNinth = triad(5);
    noNinth.seventh = Seventh::Minor;
    noNinth.eleventh = Eleventh::Perfect;
    CHECK_FALSE(noNinth.isValid());
    ChordSpec inverted = withEleventhToggled(Mode::Major, g9);
    inverted.inversion = 1;
    CHECK_FALSE(inverted.isValid());  // 11th/13th chords are root position only
    CHECK(maxInversion(withEleventhToggled(Mode::Major, g9)) == 0);
}

TEST_CASE("extensions: Roman numerals round-trip", "[ext][roman]") {
    auto v9 = withNinthToggled(Mode::Major, triad(5));
    CHECK(*formatRomanNumeral(withThirteenthToggled(Mode::Major, v9)) == "V13");
    CHECK(*formatRomanNumeral(withEleventhToggled(Mode::Major, v9)) == "V11");
    CHECK(*formatRomanNumeral(withAlterationToggled(Mode::Major, v9, Alteration::SharpNine)) == "V7#9");
    CHECK(*formatRomanNumeral(withAlterationToggled(Mode::Major, v9, Alteration::SharpEleven)) == "V9#11");
    CHECK(parseRomanNumeral("V13#11").chord.thirteenth == Thirteenth::Major);
    CHECK(parseRomanNumeral("V13#11").chord.eleventh == Eleventh::Sharp);
    CHECK_FALSE(parseRomanNumeral("V9b9#9").ok);
    CHECK_FALSE(parseRomanNumeral("V13b9b9").ok);
    CHECK_FALSE(parseRomanNumeral("V15").ok);

    // every valid extended chord: format then parse gives the same chord
    int checked = 0;
    for (int d = 1; d <= 7; ++d)
        for (auto q : {Quality::Major, Quality::Minor, Quality::Diminished, Quality::Augmented})
            for (auto sev : {Seventh::Minor, Seventh::Major})
                for (auto n : {Ninth::Minor, Ninth::Major, Ninth::Sharp})
                    for (auto e : {Eleventh::None, Eleventh::Perfect, Eleventh::Sharp})
                        for (auto t : {Thirteenth::None, Thirteenth::Minor, Thirteenth::Major})
                            for (auto sus : {Suspension::None, Suspension::Sus4}) {
                                ChordSpec c{d, 0, q, sev, 0, sus, n, e, t};
                                if (!c.isValid()) continue;
                                const auto text = formatRomanNumeral(c);
                                if (!text) continue;
                                const auto back = parseRomanNumeral(*text);
                                if (!back.ok) continue;  // grammar limits (suspension on dim/aug)
                                CHECK(back.chord == c);
                                ++checked;
                            }
    CHECK(checked > 300);
}

TEST_CASE("extensions: edit rules", "[ext][edit]") {
    const auto c = triad(5);
    const auto thirteen = withThirteenthToggled(Mode::Major, c);  // adds 7th and 9th
    CHECK(thirteen.seventh == Seventh::Minor);
    CHECK(thirteen.ninth == Ninth::Major);
    CHECK(thirteen.thirteenth == Thirteenth::Major);
    CHECK(withThirteenthToggled(Mode::Major, thirteen).thirteenth == Thirteenth::None);
    CHECK(withThirteenthToggled(Mode::Major, thirteen).ninth == Ninth::Major);  // just the 13th goes

    // 13 replaces a natural 11 and the other way round
    const auto eleven = withEleventhToggled(Mode::Major, c);
    CHECK(withThirteenthToggled(Mode::Major, eleven).eleventh == Eleventh::None);
    CHECK(withEleventhToggled(Mode::Major, thirteen).thirteenth == Thirteenth::None);

    // taking the 9th away takes everything above it; the 7th button drops to the plain 7th; Triad clears all
    CHECK(withNinthToggled(Mode::Major, thirteen).thirteenth == Thirteenth::None);
    CHECK(withSeventhToggled(Mode::Major, thirteen).ninth == Ninth::None);
    CHECK(withSeventhToggled(Mode::Major, thirteen).thirteenth == Thirteenth::None);
    CHECK(asTriad(thirteen).thirteenth == Thirteenth::None);

    // alterations toggle
    const auto sharp9 = withAlterationToggled(Mode::Major, c, Alteration::SharpNine);
    CHECK(sharp9.ninth == Ninth::Sharp);
    CHECK(withAlterationToggled(Mode::Major, sharp9, Alteration::SharpNine).ninth == Ninth::Major);
    const auto s11 = withAlterationToggled(Mode::Major, c, Alteration::SharpEleven);
    CHECK(s11.eleventh == Eleventh::Sharp);
    CHECK(withAlterationToggled(Mode::Major, s11, Alteration::SharpEleven).eleventh == Eleventh::None);

    // inversion cleared when the chord becomes extended
    auto inverted = withInversion(withNinthToggled(Mode::Major, c), 2);
    CHECK(withEleventhToggled(Mode::Major, inverted).inversion == 0);
    CHECK(withEleventhToggled(Mode::Major, inverted).isValid());

    // degree change: diatonic extensions follow, altered ones stay
    CHECK(withDegree(Mode::Major, thirteen, 3).thirteenth == Thirteenth::Minor);  // iii has the flat 13
    CHECK(withDegree(Mode::Major, sharp9, 1).ninth == Ninth::Sharp);
    CHECK(withDegree(Mode::Major, thirteen, 3).isValid());
}

TEST_CASE("extensions: mode change retargets diatonic extensions", "[ext][edit]") {
    Progression p;
    p.blocks = {{withThirteenthToggled(Mode::Major, triad(2)), 0, 3840}};  // ii13 in C major
    changeMode(p, Mode::Minor);
    CHECK(p.blocks[0].chord.thirteenth == diatonicThirteenth(Mode::Minor, 2));
    CHECK(p.blocks[0].chord.isValid());
    CHECK(p.validate().empty());
}

TEST_CASE("extensions: state round trip, text round trip, export", "[ext][state]") {
    auto g13 = withThirteenthToggled(Mode::Major, triad(5));
    auto alt = withAlterationToggled(Mode::Major, withAlterationToggled(Mode::Major, triad(5), Alteration::SharpNine), Alteration::FlatThirteen);
    Progression p;
    p.blocks = {{g13, 0, 1920}, {alt, 1920, 1920}, {withEleventhToggled(Mode::Major, triad(2)), 3840, 3840}};
    REQUIRE(p.validate().empty());

    const auto text = serializeProgression(p);
    const auto back = deserializeProgression(text);
    REQUIRE(back.ok);
    CHECK(back.progression.blocks == p.blocks);
    CHECK(text.find("\"thirteenth\":\"major\"") != std::string::npos);
    CHECK(text.find("\"ninth\":\"sharp\"") != std::string::npos);

    auto copy = formatProgressionText(p);
    const auto pasted = parseProgressionText(copy, p.timeSignature);
    REQUIRE(pasted.ok);
    CHECK(pasted.blocks == p.blocks);

    const auto generated = generateNotes(p);
    CHECK(generated.size() == 6 + 6 + 6);
    CHECK(smf::extractNotes(smf::parse(exportProgressionToMidi(p)).tracks[0]).size() == generated.size());

    // an unknown or contradictory saved chord is rejected, not guessed
    auto bad = text;
    bad.replace(bad.find("\"thirteenth\":\"major\""), 20, "\"thirteenth\":\"nope\"");
    CHECK_FALSE(deserializeProgression(bad).ok);
}

TEST_CASE("extensions: validation refuses chords that would leave MIDI range", "[ext][range]") {
    // 13th of a B-rooted chord with both octave shifts at +2: top note = 48 + 48 + 11 + 21 = 128 > 127
    Progression p;
    p.key = Key{11, Mode::Major};
    p.octave = 2;
    p.blocks = {{withThirteenthToggled(Mode::Major, triad(1)), 0, 960}};
    p.blocks[0].octave = 2;
    CHECK_FALSE(p.validate().empty());
    p.blocks[0].octave = 1;
    CHECK(p.validate().empty());
    CHECK_NOTHROW(generateNotes(p));

    // every extended chord, every key, at all octave settings: either valid (and then generates) or reported
    int refused = 0, accepted = 0;
    for (int key = 0; key < 12; ++key)
        for (int d = 1; d <= 7; ++d)
            for (int po : {-2, 0, 2})
                for (int bo : {-2, 0, 2}) {
                    Progression q;
                    q.key = Key{key, Mode::Major};
                    q.octave = po;
                    q.blocks = {{withThirteenthToggled(Mode::Major, triad(d)), 0, 960}};
                    q.blocks[0].octave = bo;
                    if (q.validate().empty()) { CHECK_NOTHROW(generateNotes(q)); ++accepted; }
                    else ++refused;
                }
    CHECK(accepted > 0);
    CHECK(refused > 0);
}
