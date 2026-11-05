// Harmonic colour and compatibility engine (spec tests A-E plus mapping rules). Expected values by hand.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/ChordEdit.h>
#include <chordcore/Grid.h>
#include <chordcore/Harmony.h>
#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>

#include "support/SmfParser.h"

#include <set>

using namespace chordcore;

namespace {
constexpr int C = 0, Db = 1, D = 2, Eb = 3, E = 4, F = 5, Gb = 6, G = 7, Ab = 8, A = 9, Bb = 10, B = 11;
const Key kCMajor{C, Mode::Major};
const auto kClassic = paletteColours(PaletteId::Classic);

std::set<int> pcs(std::uint16_t mask) {
    std::set<int> out;
    for (int i = 0; i < 12; ++i) if ((mask >> i) & 1u) out.insert(i);
    return out;
}

ChordSpec fMinorBorrowed() {
    ChordSpec c;  // iv in C major, borrowed from the minor key: Quality::Minor on degree 4
    c.degree = 4;
    c.quality = Quality::Minor;
    return c;
}
}  // namespace

TEST_CASE("Test A: functional colours in C major", "[harmony][testA]") {
    HarmonyResolver r(kCMajor, {});
    const int notes[7] = {C, D, E, F, G, A, B};
    for (int i = 0; i < 7; ++i) {
        CHECK(r.identity(notes[i]).colour == kClassic[static_cast<std::size_t>(i)]);
        CHECK(r.identity(notes[i]).diatonicDegree == i + 1);
        CHECK(r.identity(notes[i]).chromaticAlteration == 0);
    }
    // the classic palette is red, orange, gold, green, blue, purple, pink (same values as the chord cards)
    CHECK(kClassic[0] == Rgb{0xd6, 0x60, 0x5a});
    CHECK(kClassic[4] == Rgb{0x4f, 0xa0, 0xbe});
    // octave-independent
    CHECK(r.identity(60 + G).colour == r.identity(36 + G).colour);
}

TEST_CASE("colours follow the key", "[harmony]") {
    HarmonyResolver d(Key{D, Mode::Major}, {});
    CHECK(d.identity(D).colour == kClassic[0]);   // tonic is red
    CHECK(d.identity(A).colour == kClassic[4]);   // fifth is blue
    CHECK(d.identity(Db).colour == kClassic[6]);  // leading tone (C#) is pink; Db == C#
}

TEST_CASE("Test B: compatibility is independent of colour", "[harmony][testB]") {
    HarmonyResolver r(kCMajor, {});
    ChordSpec f = diatonicChord(Mode::Major, 4);
    const auto mask = chordPitchClassMask(kCMajor, f);
    const auto g = r.visual(G, mask);
    CHECK(g.noteColour == kClassic[4]);          // still blue
    CHECK_FALSE(g.isChordTone);
    CHECK(g.isScaleTone);
    CHECK(g.compatibility == Compat::ScaleTone);
    CHECK(g.backgroundColour.a == HarmonyResolver::backgroundFor(Compat::ScaleTone, g.noteColour).a);
    // the same note against the chord that contains it: colour unchanged, background changes
    const auto inG = r.visual(G, chordPitchClassMask(kCMajor, diatonicChord(Mode::Major, 5)));
    CHECK(inG.noteColour == g.noteColour);
    CHECK(inG.compatibility == Compat::ChordTone);
    CHECK(inG.backgroundColour.a > g.backgroundColour.a);
}

TEST_CASE("Test C: borrowed harmony (F minor in C major)", "[harmony][testC]") {
    HarmonyResolver r(kCMajor, {});
    const auto mask = chordPitchClassMask(kCMajor, fMinorBorrowed());
    CHECK(pcs(mask) == std::set<int>{F, Ab, C});
    for (int pc : {F, Ab, C}) CHECK(r.visual(pc, mask).isChordTone);
    const auto ab = r.visual(Ab, mask);
    CHECK(ab.compatibility == Compat::ChordTone);  // highlighted as a chord tone ...
    CHECK_FALSE(ab.isScaleTone);                    // ... while still outside the project scale
    CHECK(ab.isChromatic);
    CHECK(r.visual(G, mask).compatibility == Compat::ScaleTone);
    CHECK(r.visual(B, mask).compatibility == Compat::ScaleTone);
    CHECK(r.visual(B, mask).isScaleTone);
    CHECK(r.visual(Db, mask).compatibility == Compat::Chromatic);
}

namespace {
Progression fMinorProgression() {
    // F minor: Fm9 - Dbmaj7 - Abmaj9 - Eb7, one bar each
    Progression p;
    p.key = Key{F, Mode::Minor};
    const auto fm9 = withNinthToggled(Mode::Minor, diatonicChord(Mode::Minor, 1));
    const auto dbmaj7 = withSeventhToggled(Mode::Minor, diatonicChord(Mode::Minor, 6));
    const auto abmaj9 = withNinthToggled(Mode::Minor, diatonicChord(Mode::Minor, 3));
    const auto eb7 = withSeventhToggled(Mode::Minor, diatonicChord(Mode::Minor, 7));
    p.blocks = {{fm9, 0, 3840}, {dbmaj7, 3840, 3840}, {abmaj9, 7680, 3840}, {eb7, 11520, 3840}};
    REQUIRE(p.validate().empty());
    return p;
}
}  // namespace

TEST_CASE("Test D: several chords are visible at once, each with its own pitch classes", "[harmony][testD]") {
    const auto p = fMinorProgression();
    const auto regions = buildCompatibility(p);
    REQUIRE(regions.size() == 4);
    CHECK(pcs(regions[0].chordMask) == std::set<int>{F, Ab, C, Eb, G});
    CHECK(pcs(regions[1].chordMask) == std::set<int>{Db, F, Ab, C});
    CHECK(pcs(regions[2].chordMask) == std::set<int>{Ab, C, Eb, G, Bb});
    CHECK(pcs(regions[3].chordMask) == std::set<int>{Eb, G, Bb, Db});
    // regions meet exactly at the chord boundaries
    for (std::size_t i = 1; i < regions.size(); ++i) CHECK(regions[i].startTick == regions[i - 1].endTick);
    // looked up by tick: the highlight changes exactly on the boundary
    CHECK(regionAt(regions, 3839)->blockIndex == 0);
    CHECK(regionAt(regions, 3840)->blockIndex == 1);
    CHECK(regionAt(regions, 15359)->blockIndex == 3);
    CHECK(regionAt(regions, 15360) == nullptr);
}

TEST_CASE("Test E: an offbeat chord boundary is exact", "[harmony][testE]") {
    Progression p;
    p.blocks = {{diatonicChord(Mode::Major, 1), 0, 1440}, {diatonicChord(Mode::Major, 4), 1440, 960}};
    REQUIRE(p.validate().empty());
    const auto regions = buildCompatibility(p);
    REQUIRE(regions.size() == 2);
    CHECK(regions[0].endTick == 1440);
    CHECK(regions[1].startTick == tickOf(TimeSignature{4, 4}, 1, 2, Subdivision::And));
    CHECK(regionAt(regions, 1439)->blockIndex == 0);
    CHECK(regionAt(regions, 1440)->blockIndex == 1);
    // playback and export carry the same tick
    const auto notes = generateNotes(p);
    bool found = false;
    for (const auto& n : notes) if (n.startTick == 1440) found = true;
    CHECK(found);
    const auto exported = smf::extractNotes(smf::parse(exportProgressionToMidi(p)).tracks[0]);
    found = false;
    for (const auto& n : exported) if (n.start == 1440) found = true;
    CHECK(found);
}

TEST_CASE("gaps and unsorted blocks", "[harmony]") {
    Progression p;
    p.blocks = {{diatonicChord(Mode::Major, 5), 7680, 960}, {diatonicChord(Mode::Major, 1), 0, 960}};
    const auto regions = buildCompatibility(p);
    REQUIRE(regions.size() == 2);
    CHECK(regions[0].startTick == 0);
    CHECK(regionAt(regions, 960) == nullptr);   // the gap has no chord, so no highlight
    CHECK(regionAt(regions, 7680)->blockIndex == 0);
    CHECK(regionAt({}, 0) == nullptr);
}

TEST_CASE("extensions, alterations and suspensions are part of the chord's pitch classes", "[harmony]") {
    const auto g13 = withThirteenthToggled(Mode::Major, diatonicChord(Mode::Major, 5));  // G B D F A E
    CHECK(pcs(chordPitchClassMask(kCMajor, g13)) == std::set<int>{G, B, D, F, A, E});
    const auto g7sharp9 = withAlterationToggled(Mode::Major, diatonicChord(Mode::Major, 5), Alteration::SharpNine);  // G B D F A#
    CHECK(pcs(chordPitchClassMask(kCMajor, g7sharp9)) == std::set<int>{G, B, D, F, Bb});
    auto sus = diatonicChord(Mode::Major, 5);
    sus.suspension = Suspension::Sus4;  // G C D
    CHECK(pcs(chordPitchClassMask(kCMajor, sus)) == std::set<int>{G, C, D});
    // inversion and octave do not change pitch classes
    auto inv = withInversion(withSeventhToggled(Mode::Major, diatonicChord(Mode::Major, 5)), 2);
    CHECK(chordPitchClassMask(kCMajor, inv) == chordPitchClassMask(kCMajor, withSeventhToggled(Mode::Major, diatonicChord(Mode::Major, 5))));
}

TEST_CASE("mapping modes: A minor", "[harmony][mapping]") {
    const Key aMinor{A, Mode::Minor};
    HarmonicSettings diatonic;  // default
    HarmonicSettings major;
    major.mapping = ColourMapping::MajorCentric;
    HarmonyResolver d(aMinor, diatonic), m(aMinor, major);

    // Diatonic-Centric keeps the colours of the relative major (C): A purple, C red
    CHECK(d.identity(A).colour == kClassic[5]);
    CHECK(d.identity(C).colour == kClassic[0]);
    CHECK(d.identity(G).colour == kClassic[4]);
    // Major-Centric: tonic is red, the flat third is the orange/gold blend, the natural third would be gold
    CHECK(m.identity(A).colour == kClassic[0]);
    CHECK(m.identity(B).colour == kClassic[1]);
    const auto blend = [](Rgb a, Rgb b) { return Rgb{static_cast<std::uint8_t>((a.r + b.r + 1) / 2), static_cast<std::uint8_t>((a.g + b.g + 1) / 2), static_cast<std::uint8_t>((a.b + b.b + 1) / 2)}; };
    CHECK(m.identity(C).colour == blend(kClassic[1], kClassic[2]));  // b3
    CHECK(m.identity(G).colour == blend(kClassic[5], kClassic[6]));  // b7
    CHECK(m.identity(Eb).colour == blend(kClassic[3], kClassic[4]));  // b5
    CHECK(m.identity(F).colour == blend(kClassic[4], kClassic[5]));   // b6

    // labels
    CHECK(m.identity(C).label == "b3");
    CHECK(m.identity(C).chromaticAlteration == -1);
    CHECK(m.identity(C).diatonicDegree == 3);
    CHECK(d.identity(C).label == "3");
    CHECK(d.identity(Ab).label == "#7");  // G# in A minor
    CHECK(m.identity(Ab).label == "7");   // G# is the major-scale seventh
    CHECK(m.identity(Eb).label == "#4");
    CHECK(m.identity(Bb).label == "b2");
}

TEST_CASE("modal parents: the colouring rule takes the parent major tonic", "[harmony][mapping]") {
    // D Dorian is C major's scale: with parent tonic C, D is orange and C red
    CHECK(colourForPitchClass(C, D, PaletteId::Classic) == kClassic[1]);
    CHECK(colourForPitchClass(C, C, PaletteId::Classic) == kClassic[0]);
    // E Phrygian also has parent C: its tonic E is gold
    CHECK(colourForPitchClass(C, E, PaletteId::Classic) == kClassic[2]);
    // G Mixolydian: parent C, tonic blue; the flat seventh F is green (it is in the parent scale)
    CHECK(colourForPitchClass(C, F, PaletteId::Classic) == kClassic[3]);
    // every pitch class gets a colour for every parent
    for (int parent = 0; parent < 12; ++parent)
        for (int pc = 0; pc < 12; ++pc) (void)colourForPitchClass(parent, pc, PaletteId::Soft);
}

TEST_CASE("palettes differ and have seven distinct colours", "[harmony][palette]") {
    for (auto id : {PaletteId::Classic, PaletteId::Soft, PaletteId::ColourBlindSafe}) {
        const auto p = paletteColours(id);
        std::set<int> distinct;
        for (const auto& c : p) distinct.insert(c.r * 65536 + c.g * 256 + c.b);
        CHECK(distinct.size() == 7);
    }
    CHECK(paletteColours(PaletteId::Classic)[0] != paletteColours(PaletteId::Soft)[0]);
}

TEST_CASE("background classes are visually distinct and keep the note's hue for chord and scale tones", "[harmony]") {
    const Rgb c{100, 150, 200};
    const auto chord = HarmonyResolver::backgroundFor(Compat::ChordTone, c);
    const auto scale = HarmonyResolver::backgroundFor(Compat::ScaleTone, c);
    const auto chroma = HarmonyResolver::backgroundFor(Compat::Chromatic, c);
    CHECK(chord.a > scale.a);
    CHECK(chord.r == c.r);
    CHECK(scale.g == c.g);
    CHECK(chroma.r == chroma.g);  // neutral
}
