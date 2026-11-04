// Music-theory layer: scale degrees, chord intervals, voicing, inversions.
// Every expected value is computed by hand from the theory, not copied from program output.
// Pitches use MIDI numbering with C = pitch class 0 and the default rootBase 48 (MIDI 48 = "C3" in Live).

#include <catch2/catch_test_macros.hpp>

#include <chordcore/Theory.h>

#include <stdexcept>
#include <vector>

using namespace chordcore;
using Pitches = std::vector<int>;

namespace {
const Key kCMajor{0, Mode::Major};
const Key kAMinor{9, Mode::Minor};

ChordSpec chord(int degree, int accidental, Quality q, Seventh s = Seventh::None, int inversion = 0,
                Suspension sus = Suspension::None) {
    return ChordSpec{degree, accidental, q, s, inversion, sus};
}
}  // namespace

TEST_CASE("scaleDegreeSemitones: major and natural minor tables", "[theory]") {
    const int major[7] = {0, 2, 4, 5, 7, 9, 11};
    const int minor[7] = {0, 2, 3, 5, 7, 8, 10};
    for (int d = 1; d <= 7; ++d) {
        CHECK(scaleDegreeSemitones(Mode::Major, d) == major[d - 1]);
        CHECK(scaleDegreeSemitones(Mode::Minor, d) == minor[d - 1]);
    }
    CHECK_THROWS_AS(scaleDegreeSemitones(Mode::Major, 0), std::invalid_argument);
    CHECK_THROWS_AS(scaleDegreeSemitones(Mode::Major, 8), std::invalid_argument);
}

TEST_CASE("chordIntervals: every quality and seventh", "[theory]") {
    CHECK(chordIntervals(chord(1, 0, Quality::Major)) == Pitches{0, 4, 7});
    CHECK(chordIntervals(chord(1, 0, Quality::Minor)) == Pitches{0, 3, 7});
    CHECK(chordIntervals(chord(1, 0, Quality::Diminished)) == Pitches{0, 3, 6});
    CHECK(chordIntervals(chord(1, 0, Quality::Augmented)) == Pitches{0, 4, 8});

    CHECK(chordIntervals(chord(1, 0, Quality::Major, Seventh::Minor)) == Pitches{0, 4, 7, 10});      // dominant 7
    CHECK(chordIntervals(chord(1, 0, Quality::Major, Seventh::Major)) == Pitches{0, 4, 7, 11});      // maj7
    CHECK(chordIntervals(chord(1, 0, Quality::Minor, Seventh::Minor)) == Pitches{0, 3, 7, 10});      // m7
    CHECK(chordIntervals(chord(1, 0, Quality::Minor, Seventh::Major)) == Pitches{0, 3, 7, 11});      // mMaj7
    CHECK(chordIntervals(chord(1, 0, Quality::Diminished, Seventh::Minor)) == Pitches{0, 3, 6, 10}); // half-dim
    CHECK(chordIntervals(chord(1, 0, Quality::Diminished, Seventh::Diminished)) == Pitches{0, 3, 6, 9});
}

TEST_CASE("voiceChord: diatonic triads in C major", "[theory][pitch]") {
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major)) == Pitches{48, 52, 55});  // C E G
    CHECK(voiceChord(kCMajor, chord(2, 0, Quality::Minor)) == Pitches{50, 53, 57});  // D F A
    CHECK(voiceChord(kCMajor, chord(3, 0, Quality::Minor)) == Pitches{52, 55, 59});  // E G B
    CHECK(voiceChord(kCMajor, chord(4, 0, Quality::Major)) == Pitches{53, 57, 60});  // F A C
    CHECK(voiceChord(kCMajor, chord(5, 0, Quality::Major)) == Pitches{55, 59, 62});  // G B D
    CHECK(voiceChord(kCMajor, chord(6, 0, Quality::Minor)) == Pitches{57, 60, 64});  // A C E
    CHECK(voiceChord(kCMajor, chord(7, 0, Quality::Diminished)) == Pitches{59, 62, 65});  // B D F
}

TEST_CASE("voiceChord: the I - V - vi - IV milestone progression", "[theory][pitch]") {
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major)) == Pitches{48, 52, 55});
    CHECK(voiceChord(kCMajor, chord(5, 0, Quality::Major)) == Pitches{55, 59, 62});
    CHECK(voiceChord(kCMajor, chord(6, 0, Quality::Minor)) == Pitches{57, 60, 64});
    CHECK(voiceChord(kCMajor, chord(4, 0, Quality::Major)) == Pitches{53, 57, 60});
}

TEST_CASE("voiceChord: triad inversions", "[theory][pitch]") {
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major, Seventh::None, 0)) == Pitches{48, 52, 55});
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major, Seventh::None, 1)) == Pitches{52, 55, 60});  // E G C
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major, Seventh::None, 2)) == Pitches{55, 60, 64});  // G C E
    CHECK(voiceChord(kCMajor, chord(6, 0, Quality::Minor, Seventh::None, 1)) == Pitches{60, 64, 69});  // C E A
}

TEST_CASE("voiceChord: seventh chords and all their inversions", "[theory][pitch]") {
    // G7 in C major: G B D F
    CHECK(voiceChord(kCMajor, chord(5, 0, Quality::Major, Seventh::Minor, 0)) == Pitches{55, 59, 62, 65});
    CHECK(voiceChord(kCMajor, chord(5, 0, Quality::Major, Seventh::Minor, 1)) == Pitches{59, 62, 65, 67});  // 65
    CHECK(voiceChord(kCMajor, chord(5, 0, Quality::Major, Seventh::Minor, 2)) == Pitches{62, 65, 67, 71});  // 43
    CHECK(voiceChord(kCMajor, chord(5, 0, Quality::Major, Seventh::Minor, 3)) == Pitches{65, 67, 71, 74});  // 42

    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major, Seventh::Major)) == Pitches{48, 52, 55, 59});    // Cmaj7
    CHECK(voiceChord(kCMajor, chord(2, 0, Quality::Minor, Seventh::Minor)) == Pitches{50, 53, 57, 60});    // Dm7
    CHECK(voiceChord(kCMajor, chord(7, 0, Quality::Diminished, Seventh::Minor)) == Pitches{59, 62, 65, 69});   // Bm7b5
    CHECK(voiceChord(kCMajor, chord(7, 0, Quality::Diminished, Seventh::Diminished)) == Pitches{59, 62, 65, 68}); // Bdim7
}

TEST_CASE("voiceChord: sus2 and sus4 replace the third", "[theory][pitch][sus]") {
    const auto S2 = Suspension::Sus2;
    const auto S4 = Suspension::Sus4;
    const auto N = Seventh::None;
    CHECK(chordIntervals(chord(1, 0, Quality::Major, N, 0, S2)) == Pitches{0, 2, 7});
    CHECK(chordIntervals(chord(1, 0, Quality::Major, N, 0, S4)) == Pitches{0, 5, 7});
    CHECK(chordIntervals(chord(1, 0, Quality::Major, Seventh::Minor, 0, S4)) == Pitches{0, 5, 7, 10});

    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major, N, 0, S2)) == Pitches{48, 50, 55});  // Csus2: C D G
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major, N, 0, S4)) == Pitches{48, 53, 55});  // Csus4: C F G
    CHECK(voiceChord(kCMajor, chord(5, 0, Quality::Major, N, 0, S4)) == Pitches{55, 60, 62});  // Gsus4: G C D
    CHECK(voiceChord(kCMajor, chord(5, 0, Quality::Major, N, 0, S2)) == Pitches{55, 57, 62});  // Gsus2: G A D
    CHECK(voiceChord(kCMajor, chord(5, 0, Quality::Major, Seventh::Minor, 0, S4)) == Pitches{55, 60, 62, 65});  // G7sus4
    // quality does not change a suspended chord (the third is gone): vi sus4 = A D E
    CHECK(voiceChord(kCMajor, chord(6, 0, Quality::Minor, N, 0, S4)) == Pitches{57, 62, 64});
    // inversions
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major, N, 1, S4)) == Pitches{53, 55, 60});  // F G C
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major, N, 2, S4)) == Pitches{55, 60, 65});  // G C F
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major, N, 1, S2)) == Pitches{50, 55, 60});  // D G C
    // equality distinguishes suspensions
    CHECK(chord(1, 0, Quality::Major) != chord(1, 0, Quality::Major, N, 0, S4));
}

TEST_CASE("voiceChord: augmented triad", "[theory][pitch]") {
    CHECK(voiceChord(kCMajor, chord(3, 0, Quality::Augmented)) == Pitches{52, 56, 60});  // E G# C
}

TEST_CASE("voiceChord: natural minor key uses the minor scale's own degrees", "[theory][pitch]") {
    CHECK(voiceChord(kAMinor, chord(1, 0, Quality::Minor)) == Pitches{57, 60, 64});      // Am: A C E
    CHECK(voiceChord(kAMinor, chord(2, 0, Quality::Diminished)) == Pitches{59, 62, 65}); // Bdim
    CHECK(voiceChord(kAMinor, chord(3, 0, Quality::Major)) == Pitches{48, 52, 55});      // C
    CHECK(voiceChord(kAMinor, chord(4, 0, Quality::Minor)) == Pitches{50, 53, 57});      // Dm
    CHECK(voiceChord(kAMinor, chord(5, 0, Quality::Minor)) == Pitches{52, 55, 59});      // Em
    CHECK(voiceChord(kAMinor, chord(6, 0, Quality::Major)) == Pitches{53, 57, 60});      // F
    CHECK(voiceChord(kAMinor, chord(7, 0, Quality::Major)) == Pitches{55, 59, 62});      // G
}

TEST_CASE("voiceChord: accidentals shift the root", "[theory][pitch]") {
    CHECK(voiceChord(kCMajor, chord(7, -1, Quality::Major)) == Pitches{58, 62, 65});         // bVII = Bb
    CHECK(voiceChord(kCMajor, chord(6, -1, Quality::Major)) == Pitches{56, 60, 63});         // bVI = Ab
    CHECK(voiceChord(kCMajor, chord(3, -1, Quality::Major)) == Pitches{51, 55, 58});         // bIII = Eb
    CHECK(voiceChord(kCMajor, chord(4, +1, Quality::Diminished)) == Pitches{54, 57, 60});    // #iv° = F# A C
    CHECK(voiceChord(kCMajor, chord(1, -1, Quality::Major)) == Pitches{59, 63, 66});         // wraps below C: B
}

TEST_CASE("voiceChord: other keys transpose correctly", "[theory][pitch]") {
    CHECK(voiceChord(Key{7, Mode::Major}, chord(1, 0, Quality::Major)) == Pitches{55, 59, 62});   // G
    CHECK(voiceChord(Key{3, Mode::Major}, chord(5, 0, Quality::Major)) == Pitches{58, 62, 65});   // Eb: V = Bb
    CHECK(voiceChord(Key{6, Mode::Major}, chord(4, 0, Quality::Major)) == Pitches{59, 63, 66});   // F#: IV = B
    CHECK(voiceChord(Key{11, Mode::Major}, chord(7, 0, Quality::Diminished)) == Pitches{58, 61, 64}); // B: vii = A# C# E
}

TEST_CASE("voiceChord: voicing range is configurable", "[theory][pitch]") {
    VoicingOptions v;
    v.rootBase = 36;
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major), v) == Pitches{36, 40, 43});
    v.rootBase = 60;
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major), v) == Pitches{60, 64, 67});
}

TEST_CASE("voiceChord: determinism", "[theory]") {
    const auto c = chord(5, 0, Quality::Major, Seventh::Minor, 2);
    CHECK(voiceChord(kCMajor, c) == voiceChord(kCMajor, c));
}

TEST_CASE("voiceChord: MIDI range and argument validation", "[theory]") {
    VoicingOptions high;
    high.rootBase = 120;
    CHECK(voiceChord(kCMajor, chord(1, 0, Quality::Major), high) == Pitches{120, 124, 127});  // exactly at the limit
    CHECK_THROWS_AS(voiceChord(kCMajor, chord(5, 0, Quality::Major), high), std::out_of_range);

    VoicingOptions low;
    low.rootBase = -1;
    CHECK_THROWS_AS(voiceChord(kCMajor, chord(1, 0, Quality::Major), low), std::out_of_range);

    CHECK_THROWS_AS(voiceChord(Key{12, Mode::Major}, chord(1, 0, Quality::Major)), std::invalid_argument);
    CHECK_THROWS_AS(voiceChord(Key{-1, Mode::Major}, chord(1, 0, Quality::Major)), std::invalid_argument);
    CHECK_THROWS_AS(voiceChord(kCMajor, chord(0, 0, Quality::Major)), std::invalid_argument);
    CHECK_THROWS_AS(voiceChord(kCMajor, chord(8, 0, Quality::Major)), std::invalid_argument);
    CHECK_THROWS_AS(voiceChord(kCMajor, chord(1, 2, Quality::Major)), std::invalid_argument);
    CHECK_THROWS_AS(voiceChord(kCMajor, chord(1, 0, Quality::Major, Seventh::None, 3)), std::invalid_argument);  // triad max inv 2
    CHECK_THROWS_AS(voiceChord(kCMajor, chord(1, 0, Quality::Major, Seventh::Minor, 4)), std::invalid_argument); // 7th max inv 3
    CHECK_THROWS_AS(voiceChord(kCMajor, chord(1, 0, Quality::Major, Seventh::None, -1)), std::invalid_argument);
}

TEST_CASE("chordRootPitchClass", "[theory]") {
    CHECK(chordRootPitchClass(kCMajor, chord(5, 0, Quality::Major)) == 7);
    CHECK(chordRootPitchClass(kAMinor, chord(3, 0, Quality::Major)) == 0);
    CHECK(chordRootPitchClass(kCMajor, chord(1, -1, Quality::Major)) == 11);
    CHECK(chordRootPitchClass(Key{11, Mode::Major}, chord(7, +1, Quality::Major)) == 11);  // B + 11 + 1 = 23 -> 11
}
