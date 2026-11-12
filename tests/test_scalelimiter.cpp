// Scale Limiter: one switch for scale-only vs chromatic melody editing; never destructive.
#include <catch2/catch_test_macros.hpp>

#include <chordcore/ChordEdit.h>
#include <chordcore/Harmony.h>
#include <chordcore/MelodyEdit.h>
#include <chordcore/MelodyGenerator.h>
#include <chordcore/MidiFile.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/Progression.h>
#include <chordcore/ScaleLimiter.h>
#include <chordcore/Serialization.h>

#include "support/SmfParser.h"

#include <algorithm>
#include <set>

using namespace chordcore;

namespace {

Progression fMinor() {
    Progression p = makeDemoProgression();
    p.key.tonic = 5;  // F
    p.key.mode = Mode::Minor;
    return p;
}

std::set<int> pitchClasses(const std::vector<int>& pitches) {
    std::set<int> s;
    for (int p : pitches) s.insert(p % 12);
    return s;
}

constexpr int F = 5, G = 7, Ab = 8, Bb = 10, C = 0, Db = 1, Eb = 3;

}  // namespace

TEST_CASE("limiter A: F minor ON shows only F G Ab Bb C Db Eb", "[scalelimiter]") {
    const auto p = fMinor();
    REQUIRE(p.melodySettings.scaleLimiter);  // default ON
    const auto rows = visibleRowPitches(p.key, true);
    CHECK(pitchClasses(rows) == std::set<int>{F, G, Ab, Bb, C, Db, Eb});
    CHECK(scaleDegreeCount(p.key) == 7);
    CHECK(rows.front() == 0);   // C-1
    CHECK(rows.size() == 75);   // 7 classes over 0..127 (the 128th pitch is the top of an incomplete octave)
    CHECK(std::is_sorted(rows.begin(), rows.end()));
}

TEST_CASE("limiter B: OFF offers every pitch", "[scalelimiter]") {
    const auto rows = visibleRowPitches(fMinor().key, false);
    CHECK(rows.size() == 128);
    CHECK(pitchClasses(rows).size() == 12);
    CHECK(visibleRowPitches(fMinor().key, false, 60, 71).size() == 12);
}

TEST_CASE("limiter C: number key / degree 3 in F minor is Ab, in the octave nearest the last note", "[scalelimiter]") {
    const auto p = fMinor();
    const int pitch = pitchForScaleDegree(p.key, 3, 66, 60, 84);  // reference F#4: nearest Ab is Ab4 (68)
    CHECK(pitch == 68);
    CHECK(pitchForScaleDegree(p.key, 1, 66, 60, 84) == 65);       // F4
    CHECK(pitchForScaleDegree(p.key, 7, 62, 60, 84) == 63);       // Eb4 (nearer than Eb5)
    CHECK(pitchForScaleDegree(p.key, 3, 200, 60, 72) == 68);      // never outside the register
    CHECK(pitchForScaleDegree(p.key, 8, 66, 60, 84) == -1);       // a seven-note scale has no 8th degree
    CHECK(pitchForScaleDegree(p.key, 0, 66, 60, 84) == -1);
    CHECK(pitchForScaleDegree(p.key, 3, 66, 69, 70) == -1);       // no Ab in that register
}

TEST_CASE("limiter D: diatonic movement - G4 up one row is Ab4", "[scalelimiter]") {
    const auto key = fMinor().key;
    CHECK(stepScalePitch(key, 67, 1) == 68);   // G4 -> Ab4
    CHECK(stepScalePitch(key, 65, 1) == 67);   // F4 -> G4
    CHECK(stepScalePitch(key, 68, 1) == 70);   // Ab4 -> Bb4
    CHECK(stepScalePitch(key, 70, 1) == 72);   // Bb4 -> C5
    CHECK(stepScalePitch(key, 72, 1) == 73);   // C5 -> Db5
    CHECK(stepScalePitch(key, 67, -1) == 65);
    CHECK(stepScalePitch(key, 67, 7) == 79);   // seven degrees = one octave
    CHECK(stepScalePitch(key, 67, 0) == 67);
    // a chromatic pitch lands on the neighbouring scale pitch in the direction of travel
    CHECK(stepScalePitch(key, 69, 1) == 70);   // A4 (not in F minor) up -> Bb4
    CHECK(stepScalePitch(key, 69, -1) == 68);  // down -> Ab4
    // clamped at the ends
    CHECK(stepScalePitch(key, 127, 5) == 127);   // G9 is the top scale pitch: clamped
    CHECK(stepScalePitch(key, 0, -3) == 0);      // C-1 is the lowest
    CHECK(stepScalePitch(key, 1, -1) == 0);      // Db-1 (in scale) down one: C-1
    CHECK(pitchInScale(key, stepScalePitch(key, 100, 4)));
}

TEST_CASE("limiter D2: dragging notes by scale degrees", "[scalelimiter][edit]") {
    auto p = fMinor();
    p.melody.clear();
    REQUIRE(addMelodyNote(p, 67, 0, 960, 960).ok);   // G4
    REQUIRE(addMelodyNote(p, 70, 960, 960, 960).ok); // Bb4
    REQUIRE(moveMelodyNotesByDegrees(p, {0, 1}, 0, 0, 1, 960).ok);
    CHECK(p.melody[0].pitch == 68);   // G4 -> Ab4
    CHECK(p.melody[1].pitch == 72);   // Bb4 -> C5
    CHECK(p.melody[0].startTick == 0);
    CHECK(p.melody[1].startTick == 960);
    SECTION("time still moves, snapped") {
        REQUIRE(moveMelodyNotesByDegrees(p, {0, 1}, 0, 1920, -1, 960).ok);
        CHECK(p.melody[0].startTick == 1920);
        CHECK(p.melody[0].pitch == 67);
    }
    SECTION("a chromatic note cannot be moved by degrees (and nothing changes)") {
        auto q = fMinor();
        q.melody.clear();
        REQUIRE(addMelodyNote(q, 69, 0, 960, 960).ok);  // A4
        const auto before = q;
        CHECK_FALSE(moveMelodyNotesByDegrees(q, {0}, 0, 0, 1, 960).ok);
        CHECK(q == before);
    }
    SECTION("clamped at the top of the range, group stays rigid in degrees") {
        const auto r = moveMelodyNotesByDegrees(p, {0, 1}, 0, 0, 500, 960);
        REQUIRE(r.ok);
        CHECK(pitchInScale(p.key, p.melody[0].pitch));
        CHECK(pitchInScale(p.key, p.melody[1].pitch));
        CHECK(p.melody[1].pitch <= 127);
    }
}

TEST_CASE("limiter E: chromatic movement - G4 up one semitone is Ab4", "[scalelimiter][edit]") {
    auto p = fMinor();
    p.melody.clear();
    REQUIRE(addMelodyNote(p, 67, 0, 960, 960).ok);
    REQUIRE(moveMelodyNotes(p, {0}, 0, 0, 1, 960).ok);
    CHECK(p.melody[0].pitch == 68);
}

TEST_CASE("limiter F: hidden notes stay in the model, untouched by the switch", "[scalelimiter]") {
    auto p = fMinor();
    p.melody.clear();
    p.melodySettings.scaleLimiter = false;
    REQUIRE(addMelodyNote(p, 69, 480, 240, 480, 77).ok);  // A4: not in F minor
    REQUIRE(addMelodyNote(p, 65, 0, 240, 480, 90).ok);    // F4
    const auto before = p.melody;

    p.melodySettings.scaleLimiter = true;
    CHECK(p.melody == before);                       // the model is identical
    CHECK(outOfScaleNoteCount(p) == 1);
    const auto editable = editableNoteIndices(p);
    REQUIRE(editable.size() == 1);
    CHECK(p.melody[editable[0]].pitch == 65);        // only F4 is shown / editable

    p.melodySettings.scaleLimiter = false;
    CHECK(editableNoteIndices(p).size() == 2);
    CHECK(p.melody == before);                       // A4 back with identical pitch, timing, length, velocity
    CHECK(p.melody[1].pitch == 69);
    CHECK(p.melody[1].velocity == 77);
}

TEST_CASE("limiter G: hidden notes are exported and played", "[scalelimiter][export]") {
    auto p = fMinor();
    p.melody.clear();
    p.melodySettings.scaleLimiter = false;
    REQUIRE(addMelodyNote(p, 65, 0, 480, 480).ok);    // F4
    REQUIRE(addMelodyNote(p, 69, 480, 480, 480).ok);  // A4
    p.melodySettings.scaleLimiter = true;

    ProjectExportOptions o;
    o.part = ExportPart::Melody;
    const auto file = smf::parse(exportProject(p, o));
    REQUIRE(file.tracks.size() == 1);
    const auto notes = smf::extractNotes(file.tracks[0]);
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].pitch == 65);
    CHECK(notes[1].pitch == 69);
    CHECK(notes[0].channel == 1);  // melody stays on channel 2 (index 1)
    CHECK(notes[1].channel == 1);

    // playback reads the same event list
    const auto events = generateProjectNotes(p);
    int melodyEvents = 0;
    for (const auto& e : events) if (e.channel == 1) { ++melodyEvents; CHECK((e.pitch == 65 || e.pitch == 69)); }
    CHECK(melodyEvents == 2);

    // ON and OFF export byte-identical files
    auto q = p;
    q.melodySettings.scaleLimiter = false;
    CHECK(exportProject(q, o) == exportProject(p, o));
}

TEST_CASE("limiter H: borrowed chord - Ab in Fm over C major is a chord tone with no row", "[scalelimiter]") {
    Progression p = makeDemoProgression();
    p.key.tonic = 0;
    p.key.mode = Mode::Major;
    ChordSpec fm = diatonicChord(Mode::Major, 4);  // F major (IV)
    fm = withQuality(fm, Quality::Minor);
    const auto mask = chordPitchClassMask(p.key, fm);
    REQUIRE(mask == ((1u << F) | (1u << Ab) | (1u << C)));
    CHECK(chromaticChordToneCount(p.key, fm) == 1);

    const auto rows = visibleRowPitches(p.key, true);
    CHECK(pitchClasses(rows).count(Ab) == 0);          // no Ab row while limited
    CHECK(pitchClasses(rows).count(F) == 1);
    CHECK(pitchClasses(rows).count(C) == 1);

    HarmonicSettings hs;
    HarmonyResolver resolver(p.key, hs);
    CHECK(resolver.classify(F, mask) == Compat::ChordTone);   // visible chord tones are highlighted
    CHECK(resolver.classify(C, mask) == Compat::ChordTone);
    CHECK(resolver.classify(Ab, mask) == Compat::ChordTone);  // Ab stays a chord tone internally

    const auto all = visibleRowPitches(p.key, false);
    CHECK(pitchClasses(all).count(Ab) == 1);                  // OFF: Ab row exists and is highlighted via the same classifier
    CHECK(chromaticChordToneCount(p.key, diatonicChord(Mode::Major, 1)) == 0);
}

namespace {
Progression chromaticProgression() {
    Progression p = makeDemoProgression();
    p.key.tonic = 0;
    p.key.mode = Mode::Major;
    p.melody.clear();
    // I  bVI(Ab)  bVII(Bb)  iv(Fm): borrowed chords full of chromatic tones
    const ChordSpec fm = withQuality(diatonicChord(Mode::Major, 4), Quality::Minor);
    ChordSpec ab = withRootPitchClass(p.key, diatonicChord(Mode::Major, 1), 8);
    ChordSpec bb = withRootPitchClass(p.key, diatonicChord(Mode::Major, 1), 10);
    p.blocks[0].chord = diatonicChord(Mode::Major, 1);
    p.blocks[1].chord = ab;
    p.blocks[2].chord = bb;
    p.blocks[3].chord = fm;
    return p;
}
}  // namespace

TEST_CASE("limiter I: generation with the limiter ON stays in the scale, even at maximum tension", "[scalelimiter][generator]") {
    for (int tension : {0, 50, 100}) {
        for (std::uint32_t seed = 1; seed <= 6; ++seed) {
            auto p = chromaticProgression();
            p.melodySettings.scaleLimiter = true;
            p.melodySettings.generator.tension = tension;
            p.melodySettings.generator.seed = seed;
            const auto range = scopeRange(p, PhraseScope::FullProgression, 1, 0, 0);
            const auto notes = generateMelodyForRange(p, p.melodySettings.generator, range);
            REQUIRE(!notes.empty());
            for (const auto& n : notes) CHECK(pitchInScale(p.key, n.pitch));
            auto q = p;  // Regenerate (the OpResult path) obeys it too
            REQUIRE(regenerateMelody(q, range).ok);
            for (const auto& n : q.melody) CHECK(pitchInScale(q.key, n.pitch));
        }
    }
}

TEST_CASE("limiter I2: OFF still produces chromatic notes at high tension (the old behaviour)", "[scalelimiter][generator]") {
    bool sawChromatic = false;
    for (std::uint32_t seed = 1; seed <= 20 && !sawChromatic; ++seed) {
        auto p = chromaticProgression();
        p.melodySettings.scaleLimiter = false;
        p.melodySettings.generator.tension = 100;
        p.melodySettings.generator.seed = seed;
        const auto notes = generateMelodyForRange(p, p.melodySettings.generator, scopeRange(p, PhraseScope::FullProgression, 1, 0, 0));
        for (const auto& n : notes) sawChromatic = sawChromatic || !pitchInScale(p.key, n.pitch);
    }
    CHECK(sawChromatic);
}

TEST_CASE("limiter J: turning the limiter ON never touches existing chromatic notes (incl. regenerate with locks)", "[scalelimiter][generator]") {
    auto p = chromaticProgression();
    p.melodySettings.scaleLimiter = false;
    p.melodySettings.generator.tension = 100;
    const auto range = scopeRange(p, PhraseScope::FullProgression, 1, 0, 0);
    REQUIRE(regenerateMelody(p, range).ok);
    for (auto& n : p.melody) n.locked = true;  // lock what was generated, chromatic notes included
    const auto before = p.melody;
    p.melodySettings.scaleLimiter = true;
    CHECK(p.melody == before);                 // the switch itself changes nothing
    REQUIRE(regenerateMelody(p, range).ok);    // locked notes survive regeneration with the limiter ON
    CHECK(p.melody == before);
}

TEST_CASE("limiter K: save / reload keeps the switch and every note; ON is the default and omitted", "[scalelimiter][serialization]") {
    auto p = fMinor();
    p.melody.clear();
    p.melodySettings.scaleLimiter = false;
    REQUIRE(addMelodyNote(p, 69, 0, 240, 480).ok);  // A4, chromatic
    p.melodySettings.scaleLimiter = true;
    auto text = serializeProgression(p);
    CHECK(text.find("scaleLimiter") == std::string::npos);  // default ON is not written: old projects stay byte-identical
    auto back = deserializeProgression(text);
    REQUIRE(back.ok);
    CHECK(back.progression.melodySettings.scaleLimiter);
    CHECK(back.progression.melody == p.melody);

    p.melodySettings.scaleLimiter = false;
    text = serializeProgression(p);
    CHECK(text.find("\"scaleLimiter\":false") != std::string::npos);
    back = deserializeProgression(text);
    REQUIRE(back.ok);
    CHECK_FALSE(back.progression.melodySettings.scaleLimiter);
    CHECK(back.progression == p);

    // a project saved before the switch existed loads as ON
    const auto legacy = deserializeProgression(serializeProgression(fMinor()));
    REQUIRE(legacy.ok);
    CHECK(legacy.progression.melodySettings.scaleLimiter);
}

TEST_CASE("limiter L: toggling repeatedly never changes onsets, lengths, velocities or pitches", "[scalelimiter]") {
    auto p = fMinor();
    p.melody.clear();
    p.melodySettings.scaleLimiter = false;
    for (int i = 0; i < 16; ++i) {
        const Tick start = i % 2 == 0 ? i * 240 : i * 240 + 120;  // eighth and sixteenth positions
        REQUIRE(addMelodyNote(p, 60 + (i * 5) % 12, start, 120, 120, 40 + i).ok);
    }
    const auto before = p.melody;
    for (int i = 0; i < 25; ++i) {
        p.melodySettings.scaleLimiter = !p.melodySettings.scaleLimiter;
        (void)editableNoteIndices(p);
        (void)outOfScaleNoteCount(p);
    }
    CHECK(p.melody == before);
}

TEST_CASE("limiter: non-heptatonic masks adapt the row count (no invented degrees)", "[scalelimiter]") {
    // The theory engine only has major and minor, so this pins the 7-row behaviour for both.
    Key major;
    major.tonic = 0;
    major.mode = Mode::Major;
    CHECK(scaleDegreeCount(major) == 7);
    CHECK(visibleRowPitches(major, true, 60, 71).size() == 7);
    CHECK(visibleRowPitches(major, true, 60, 71) == std::vector<int>{60, 62, 64, 65, 67, 69, 71});
}
