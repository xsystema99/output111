// Palette / type-button rules, scale switching, and chord display names.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/ChordEdit.h>
#include <chordcore/Progression.h>
#include <chordcore/Theory.h>

#include <random>
#include <set>
#include <stdexcept>

using namespace chordcore;

namespace {

ChordSpec chord(int degree, Quality q, Seventh s = Seventh::None, int inversion = 0, Suspension sus = Suspension::None,
                int accidental = 0) {
    return ChordSpec{degree, accidental, q, s, inversion, sus};
}

}  // namespace

TEST_CASE("diatonicQuality: the seven triads of each scale", "[chordedit]") {
    const Quality M = Quality::Major, m = Quality::Minor, d = Quality::Diminished;
    const Quality major[7] = {M, m, m, M, M, m, d};
    const Quality minor[7] = {m, d, M, m, m, M, M};
    for (int degree = 1; degree <= 7; ++degree) {
        CHECK(diatonicQuality(Mode::Major, degree) == major[degree - 1]);
        CHECK(diatonicQuality(Mode::Minor, degree) == minor[degree - 1]);
    }
    CHECK_THROWS_AS(diatonicQuality(Mode::Major, 0), std::invalid_argument);
    CHECK_THROWS_AS(diatonicSeventh(Mode::Minor, 8), std::invalid_argument);
}

TEST_CASE("diatonicSeventh: the seven seventh chords of each scale", "[chordedit]") {
    const Seventh maj = Seventh::Major, min = Seventh::Minor;
    const Seventh major[7] = {maj, min, min, maj, min, min, min};
    const Seventh minor[7] = {min, min, maj, min, min, maj, min};
    for (int degree = 1; degree <= 7; ++degree) {
        CHECK(diatonicSeventh(Mode::Major, degree) == major[degree - 1]);
        CHECK(diatonicSeventh(Mode::Minor, degree) == minor[degree - 1]);
    }
}

TEST_CASE("diatonic tables agree with the scale itself (independent check)", "[chordedit]") {
    // Every chord tone of every diatonic triad and seventh chord must be a note of the scale.
    for (Mode mode : {Mode::Major, Mode::Minor}) {
        std::set<int> scale;
        for (int degree = 1; degree <= 7; ++degree) scale.insert(scaleDegreeSemitones(mode, degree) % 12);
        for (int degree = 1; degree <= 7; ++degree) {
            auto spec = diatonicChord(mode, degree);
            const int root = scaleDegreeSemitones(mode, degree);
            for (int withSeventh = 0; withSeventh < 2; ++withSeventh) {
                if (withSeventh) spec.seventh = diatonicSeventh(mode, degree);
                for (int interval : chordIntervals(spec)) {
                    INFO("mode " << (mode == Mode::Major ? "major" : "minor") << " degree " << degree
                                 << " seventh " << withSeventh << " interval " << interval);
                    CHECK(scale.count((root + interval) % 12) == 1);
                }
            }
        }
    }
}

TEST_CASE("diatonicChord and isDiatonic", "[chordedit]") {
    const auto c = diatonicChord(Mode::Minor, 6);
    CHECK(c == chord(6, Quality::Major));
    CHECK(isDiatonic(Mode::Minor, c));
    CHECK_FALSE(isDiatonic(Mode::Major, c));                                     // VI is minor in major
    CHECK(isDiatonic(Mode::Minor, chord(6, Quality::Major, Seventh::Major, 2, Suspension::Sus4)));  // type is ignored
    CHECK_FALSE(isDiatonic(Mode::Major, chord(7, Quality::Major, Seventh::None, 0, Suspension::None, -1)));  // bVII
    CHECK_FALSE(isDiatonic(Mode::Major, chord(5, Quality::Minor)));              // v in major is borrowed
    CHECK_FALSE(isDiatonic(Mode::Major, chord(9, Quality::Major)));
}

TEST_CASE("withDegree: palette click keeps the chord's type", "[chordedit]") {
    SECTION("a triad becomes the diatonic triad") {
        CHECK(withDegree(Mode::Minor, chord(1, Quality::Minor), 3) == chord(3, Quality::Major));
        CHECK(withDegree(Mode::Minor, chord(1, Quality::Minor), 2) == chord(2, Quality::Diminished));
        CHECK(withDegree(Mode::Major, chord(1, Quality::Major), 6) == chord(6, Quality::Minor));
    }
    SECTION("a seventh stays a seventh, using the new degree's own") {
        const auto r = withDegree(Mode::Major, chord(1, Quality::Major, Seventh::Major), 5);
        CHECK(r == chord(5, Quality::Major, Seventh::Minor));  // Imaj7 -> V7
        CHECK(withDegree(Mode::Minor, chord(1, Quality::Minor, Seventh::Minor), 6) == chord(6, Quality::Major, Seventh::Major));
    }
    SECTION("a suspension stays, except where it is impossible") {
        CHECK(withDegree(Mode::Major, chord(1, Quality::Major, Seventh::None, 0, Suspension::Sus4), 5) ==
              chord(5, Quality::Major, Seventh::None, 0, Suspension::Sus4));
        CHECK(withDegree(Mode::Major, chord(1, Quality::Major, Seventh::None, 0, Suspension::Sus4), 7) ==
              chord(7, Quality::Diminished));  // vii° cannot be suspended
    }
    SECTION("accidental is cleared; the inversion is kept when it still exists") {
        CHECK(withDegree(Mode::Major, chord(7, Quality::Major, Seventh::None, 0, Suspension::None, -1), 4).accidental == 0);
        CHECK(withDegree(Mode::Major, chord(1, Quality::Major, Seventh::None, 2), 4).inversion == 2);
        CHECK(withDegree(Mode::Major, chord(1, Quality::Major, Seventh::Major, 3), 4).inversion == 3);
    }
    SECTION("always a valid chord") {
        for (Mode mode : {Mode::Major, Mode::Minor})
            for (int from = 1; from <= 7; ++from)
                for (int to = 1; to <= 7; ++to)
                    for (int s = 0; s < 4; ++s)
                        for (int sus = 0; sus < 3; ++sus) {
                            auto c = diatonicChord(mode, from);
                            c.seventh = static_cast<Seventh>(s);
                            c.suspension = static_cast<Suspension>(sus);
                            c.inversion = c.seventh == Seventh::None ? 2 : 3;
                            if (!canSuspend(c)) c.suspension = Suspension::None;
                            const auto r = withDegree(mode, c, to);
                            CHECK(r.isValid());
                            CHECK(r.degree == to);
                        }
    }
}

TEST_CASE("type buttons: triad, 7th, sus", "[chordedit]") {
    SECTION("7th toggles the diatonic seventh") {
        auto c = chord(5, Quality::Major);
        c = withSeventhToggled(Mode::Major, c);
        CHECK(c == chord(5, Quality::Major, Seventh::Minor));  // V7
        c = withSeventhToggled(Mode::Major, c);
        CHECK(c == chord(5, Quality::Major));
        CHECK(withSeventhToggled(Mode::Major, chord(1, Quality::Major)).seventh == Seventh::Major);
        CHECK(withSeventhToggled(Mode::Minor, chord(7, Quality::Major)).seventh == Seventh::Minor);
        CHECK(withSeventhToggled(Mode::Major, chord(7, Quality::Diminished)).seventh == Seventh::Minor);  // viiø7
    }
    SECTION("7th keeps a suspension and the inversion; removing it clamps the inversion") {
        auto c = withSeventhToggled(Mode::Major, chord(5, Quality::Major, Seventh::None, 2, Suspension::Sus4));
        CHECK(c.seventh == Seventh::Minor);
        CHECK(c.suspension == Suspension::Sus4);
        CHECK(c.inversion == 2);
        auto d = withSeventhToggled(Mode::Major, chord(5, Quality::Major, Seventh::Minor, 3));
        CHECK(d.seventh == Seventh::None);
        CHECK(d.inversion == 2);
        CHECK(d.isValid());
    }
    SECTION("sus toggles and switches") {
        auto c = chord(1, Quality::Major);
        c = withSuspensionToggled(c, Suspension::Sus4);
        CHECK(c.suspension == Suspension::Sus4);
        c = withSuspensionToggled(c, Suspension::Sus2);
        CHECK(c.suspension == Suspension::Sus2);
        c = withSuspensionToggled(c, Suspension::Sus2);
        CHECK(c.suspension == Suspension::None);
    }
    SECTION("sus is refused on diminished and augmented chords") {
        const auto dim = chord(7, Quality::Diminished);
        CHECK_FALSE(canSuspend(dim));
        CHECK(withSuspensionToggled(dim, Suspension::Sus4) == dim);
        const auto aug = chord(3, Quality::Augmented);
        CHECK_FALSE(canSuspend(aug));
        CHECK(withSuspensionToggled(aug, Suspension::Sus2) == aug);
        CHECK(canSuspend(chord(1, Quality::Major)));
        CHECK(canSuspend(chord(2, Quality::Minor)));
    }
    SECTION("triad clears seventh and suspension") {
        const auto c = asTriad(chord(5, Quality::Major, Seventh::Minor, 3, Suspension::Sus4));
        CHECK(c == chord(5, Quality::Major, Seventh::None, 2));
        CHECK(asTriad(chord(1, Quality::Major)) == chord(1, Quality::Major));
    }
}

TEST_CASE("changeMode: diatonic chords follow the scale, borrowed chords stay", "[chordedit]") {
    SECTION("the demo in C major becomes C minor: i v VI iv") {
        auto p = makeDemoProgression();
        changeMode(p, Mode::Minor);
        CHECK(p.key == Key{0, Mode::Minor});
        CHECK(p.blocks[0].chord == chord(1, Quality::Minor));
        CHECK(p.blocks[1].chord == chord(5, Quality::Minor));
        CHECK(p.blocks[2].chord == chord(6, Quality::Major));
        CHECK(p.blocks[3].chord == chord(4, Quality::Minor));
        CHECK(p.validate().empty());
    }
    SECTION("sevenths: native ones follow, chosen ones stay") {
        Progression p;
        p.blocks = {
            ChordBlock{chord(1, Quality::Major, Seventh::Major), 0, 3840},      // Imaj7 -> i7
            ChordBlock{chord(4, Quality::Major, Seventh::Minor), 3840, 3840},   // IV with a minor 7th (not native): stays
            ChordBlock{chord(6, Quality::Minor, Seventh::Minor), 7680, 3840},   // vi7 -> VImaj7
        };
        changeMode(p, Mode::Minor);
        CHECK(p.blocks[0].chord == chord(1, Quality::Minor, Seventh::Minor));
        CHECK(p.blocks[1].chord == chord(4, Quality::Minor, Seventh::Minor));
        CHECK(p.blocks[2].chord == chord(6, Quality::Major, Seventh::Major));
    }
    SECTION("borrowed and altered chords are untouched") {
        Progression p;
        p.key.mode = Mode::Minor;
        p.blocks = {
            ChordBlock{chord(5, Quality::Major), 0, 3840},                                      // V in minor (not diatonic)
            ChordBlock{chord(7, Quality::Major, Seventh::None, 0, Suspension::None, -1), 3840, 3840},  // bVII
            ChordBlock{chord(3, Quality::Augmented), 7680, 3840},
        };
        const auto blocks = p.blocks;
        changeMode(p, Mode::Major);
        CHECK(p.key.mode == Mode::Major);
        CHECK(p.blocks == blocks);
    }
    SECTION("a suspended chord that would become diminished loses its suspension") {
        Progression p;
        p.key.mode = Mode::Minor;
        p.blocks = {ChordBlock{chord(2, Quality::Diminished), 0, 3840}};  // ii° in minor
        changeMode(p, Mode::Major);
        CHECK(p.blocks[0].chord == chord(2, Quality::Minor));
        CHECK(p.validate().empty());
    }
    SECTION("same mode is a no-op") {
        auto p = makeDemoProgression();
        changeMode(p, Mode::Major);
        CHECK(p == makeDemoProgression());
    }
}

TEST_CASE("property: major -> minor -> major restores a diatonic progression", "[chordedit][property]") {
    std::mt19937 rng(77);
    for (int trial = 0; trial < 200; ++trial) {
        Progression p;
        p.key = Key{static_cast<int>(rng() % 12), rng() % 2 ? Mode::Major : Mode::Minor};
        Tick cursor = 0;
        for (int i = 0, n = 1 + static_cast<int>(rng() % 8); i < n; ++i) {
            auto c = diatonicChord(p.key.mode, 1 + static_cast<int>(rng() % 7));
            if (rng() % 2) c.seventh = diatonicSeventh(p.key.mode, c.degree);
            if (rng() % 3 == 0 && canSuspend(c)) c.suspension = rng() % 2 ? Suspension::Sus2 : Suspension::Sus4;
            c.inversion = static_cast<int>(rng() % (c.seventh == Seventh::None ? 3 : 4));
            p.blocks.push_back(ChordBlock{c, cursor, 960});
            cursor += 960;
        }
        const auto original = p;
        const Mode other = p.key.mode == Mode::Major ? Mode::Minor : Mode::Major;
        changeMode(p, other);
        REQUIRE(p.validate().empty());
        changeMode(p, original.key.mode);
        // Sus on a degree that is diminished in the other scale loses its suspension (documented), so
        // compare everything else exactly.
        for (std::size_t i = 0; i < p.blocks.size(); ++i) {
            auto expected = original.blocks[i].chord;
            auto actual = p.blocks[i].chord;
            expected.suspension = actual.suspension = Suspension::None;
            REQUIRE(actual == expected);
        }
    }
}

TEST_CASE("noteName: flats and sharps follow the key signature", "[chordedit][names]") {
    const Key cMajor{0, Mode::Major}, fMajor{5, Mode::Major}, gMajor{7, Mode::Major}, dMinor{2, Mode::Minor},
        aMinor{9, Mode::Minor}, bbMinor{10, Mode::Minor}, eMinor{4, Mode::Minor};
    CHECK(noteName(cMajor, 1) == "C#");
    CHECK(noteName(fMajor, 10) == "Bb");
    CHECK(noteName(gMajor, 6) == "F#");
    CHECK(noteName(dMinor, 10) == "Bb");   // D minor shares F major's signature
    CHECK(noteName(aMinor, 8) == "G#");
    CHECK(noteName(eMinor, 6) == "F#");
    CHECK(noteName(bbMinor, 8) == "Ab");   // Bb minor: relative major Db
    CHECK(noteName(cMajor, -1) == "B");
    CHECK(noteName(cMajor, 13) == "C#");
    CHECK(noteName(Key{6, Mode::Major}, 6) == "F#");  // F# major uses sharps
    CHECK(noteName(Key{3, Mode::Major}, 3) == "Eb");
}

TEST_CASE("chordName: the mockup's D minor progression", "[chordedit][names]") {
    const Key dMinor{2, Mode::Minor};
    CHECK(chordName(dMinor, diatonicChord(Mode::Minor, 3)) == "F");
    CHECK(chordName(dMinor, diatonicChord(Mode::Minor, 6)) == "Bb");
    CHECK(chordName(dMinor, diatonicChord(Mode::Minor, 7)) == "C");
    CHECK(chordName(dMinor, diatonicChord(Mode::Minor, 1)) == "Dm");
    CHECK(chordName(dMinor, diatonicChord(Mode::Minor, 2)) == "Edim");
    CHECK(chordName(dMinor, diatonicChord(Mode::Minor, 4)) == "Gm");
    CHECK(chordName(dMinor, diatonicChord(Mode::Minor, 5)) == "Am");
}

TEST_CASE("chordName: qualities, sevenths, suspensions, inversions", "[chordedit][names]") {
    const Key c{0, Mode::Major};
    CHECK(chordName(c, chord(1, Quality::Major)) == "C");
    CHECK(chordName(c, chord(2, Quality::Minor)) == "Dm");
    CHECK(chordName(c, chord(7, Quality::Diminished)) == "Bdim");
    CHECK(chordName(c, chord(3, Quality::Augmented)) == "Eaug");
    CHECK(chordName(c, chord(5, Quality::Major, Seventh::Minor)) == "G7");
    CHECK(chordName(c, chord(1, Quality::Major, Seventh::Major)) == "Cmaj7");
    CHECK(chordName(c, chord(2, Quality::Minor, Seventh::Minor)) == "Dm7");
    CHECK(chordName(c, chord(1, Quality::Minor, Seventh::Major)) == "Cm(maj7)");
    CHECK(chordName(c, chord(7, Quality::Diminished, Seventh::Minor)) == "Bm7b5");
    CHECK(chordName(c, chord(7, Quality::Diminished, Seventh::Diminished)) == "Bdim7");
    CHECK(chordName(c, chord(5, Quality::Major, Seventh::None, 0, Suspension::Sus4)) == "Gsus4");
    CHECK(chordName(c, chord(5, Quality::Major, Seventh::None, 0, Suspension::Sus2)) == "Gsus2");
    CHECK(chordName(c, chord(2, Quality::Minor, Seventh::None, 0, Suspension::Sus4)) == "Dsus4");  // no third: quality is moot
    CHECK(chordName(c, chord(5, Quality::Major, Seventh::Minor, 0, Suspension::Sus4)) == "G7sus4");
    CHECK(chordName(c, chord(1, Quality::Major, Seventh::None, 1)) == "C/E");
    CHECK(chordName(c, chord(1, Quality::Major, Seventh::None, 2)) == "C/G");
    CHECK(chordName(c, chord(5, Quality::Major, Seventh::Minor, 1)) == "G7/B");
    CHECK(chordName(c, chord(5, Quality::Major, Seventh::Minor, 3)) == "G7/F");
    CHECK(chordName(c, chord(7, Quality::Major, Seventh::None, 0, Suspension::None, -1)) == "Bb");  // bVII in C: flats despite the sharp key
    CHECK(chordName(c, chord(4, Quality::Major, Seventh::None, 0, Suspension::None, +1)) == "F#");    // #IV
    CHECK(chordName(Key{5, Mode::Major}, chord(4, Quality::Major, Seventh::None, 0, Suspension::None, +1)) == "B");  // #IV in F
    CHECK(chordName(c, chord(9, Quality::Major)) == "?");
}

TEST_CASE("chordName: spelled in the key's accidentals", "[chordedit][names]") {
    CHECK(chordName(Key{5, Mode::Major}, diatonicChord(Mode::Major, 4)) == "Bb");         // F major: IV
    CHECK(chordName(Key{7, Mode::Major}, diatonicChord(Mode::Major, 7)) == "F#dim");     // G major: vii°
    CHECK(chordName(Key{3, Mode::Major}, diatonicChord(Mode::Major, 3)) == "Gm");        // Eb major: iii
    CHECK(chordName(Key{10, Mode::Minor}, diatonicChord(Mode::Minor, 6)) == "Gb");       // Bb minor: VI
}
