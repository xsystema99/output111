// MIDI filename format: Absolute / Roman / Both, one generator for every export path.

#include <catch2/catch_test_macros.hpp>

#include <chordcore/ChordEdit.h>
#include <chordcore/ChromaticDice.h>
#include <chordcore/MidiFile.h>
#include <chordcore/MidiFilename.h>
#include <chordcore/NoteEngine.h>
#include <chordcore/RomanNumeral.h>
#include <chordcore/Serialization.h>

#include "support/SmfParser.h"

using namespace chordcore;

namespace {

const std::string kDash = "\xE2\x80\x93";
const std::string kFlat = "\xE2\x99\xAD";

ChordSpec chordAt(const Key& key, int semitones, Quality q, Seventh s = Seventh::None, Ninth n = Ninth::None) {
    ChordSpec c;
    REQUIRE(spellRoot(key, semitones, c.degree, c.accidental));
    c.quality = q;
    c.seventh = s;
    c.ninth = n;
    return c;
}

// Fm9 - Dbmaj7 - Abmaj9 - Eb7 in F minor
Progression fMinor() {
    Progression p;
    p.key = Key{5, Mode::Minor};
    const Tick bar = 3840;
    p.blocks.push_back(ChordBlock{chordAt(p.key, 0, Quality::Minor, Seventh::Minor, Ninth::Major), 0, bar});
    p.blocks.push_back(ChordBlock{chordAt(p.key, 8, Quality::Major, Seventh::Major), bar, bar});
    p.blocks.push_back(ChordBlock{chordAt(p.key, 3, Quality::Major, Seventh::Major, Ninth::Major), 2 * bar, bar});
    p.blocks.push_back(ChordBlock{chordAt(p.key, 10, Quality::Major, Seventh::Minor), 3 * bar, bar});
    return p;
}

ProjectExportOptions chordsOriginal() {
    ProjectExportOptions o;
    o.part = ExportPart::Chords;
    o.originalVoicing = true;  // no suffix: the plain progression name
    return o;
}

std::string name(const Progression& p, MidiFilenameFormat f, const ProjectExportOptions& o = chordsOriginal(), bool ascii = false) {
    return generateMidiFilename(p, f, o, ascii);
}

}  // namespace

TEST_CASE("filename A/B/C: absolute, roman and both for Fm9 - Dbmaj7 - Abmaj9 - Eb7 in F minor", "[filename]") {
    const auto p = fMinor();
    const std::string d = " " + kDash + " ";
    CHECK(name(p, MidiFilenameFormat::Absolute) == "Fm9" + d + "D" + kFlat + "maj7" + d + "A" + kFlat + "maj9" + d + "E" + kFlat + "7.mid");
    CHECK(name(p, MidiFilenameFormat::Roman) == "i9" + d + kFlat + "VImaj7" + d + kFlat + "IIImaj9" + d + kFlat + "VII7.mid");
    CHECK(name(p, MidiFilenameFormat::Both) == "Fm9 (i9)" + d + "D" + kFlat + "maj7 (" + kFlat + "VImaj7)" + d + "A" + kFlat + "maj9 (" + kFlat + "IIImaj9)" + d + "E" +
                                                   kFlat + "7 (" + kFlat + "VII7).mid");
    // the ASCII fallback is the same names in plain characters
    CHECK(name(p, MidiFilenameFormat::Both, chordsOriginal(), true) == "Fm9 (i9) - Dbmaj7 (bVImaj7) - Abmaj9 (bIIImaj9) - Eb7 (bVII7).mid");
    CHECK(chromaticRomanNumeral(p.key, p.blocks[1].chord).value() == "bVImaj7");  // not the diatonic "VImaj7"
}

TEST_CASE("filename: Roman numerals are chromatic relative to the tonic in both modes", "[filename]") {
    for (Mode mode : {Mode::Major, Mode::Minor}) {
        const Key key{0, mode};
        CHECK(chromaticRomanNumeral(key, chordAt(key, 1, Quality::Major)).value() == "bII");
        CHECK(chromaticRomanNumeral(key, chordAt(key, 3, Quality::Major)).value() == "bIII");
        CHECK(chromaticRomanNumeral(key, chordAt(key, 5, Quality::Minor)).value() == "iv");
        CHECK(chromaticRomanNumeral(key, chordAt(key, 6, Quality::Diminished)).value() == "#ivo");
        CHECK(chromaticRomanNumeral(key, chordAt(key, 7, Quality::Minor)).value() == "v");
        CHECK(chromaticRomanNumeral(key, chordAt(key, 8, Quality::Major, Seventh::Major)).value() == "bVImaj7");
        CHECK(chromaticRomanNumeral(key, chordAt(key, 10, Quality::Major)).value() == "bVII");
    }
}

TEST_CASE("filename D: Chromatic Dice replacing a chord updates every format", "[filename][dice]") {
    auto p = fMinor();
    const auto before = std::vector<std::string>{name(p, MidiFilenameFormat::Absolute), name(p, MidiFilenameFormat::Roman), name(p, MidiFilenameFormat::Both)};
    DiceSettings s;
    s.mode = DiceMode::ReplaceHarmony;
    s.complexity = DiceComplexity::Adventurous;
    const auto r = rollChromaticDice(p, 1, s);
    REQUIRE(r.ok);
    p.blocks[1].chord = r.chord;
    const std::vector<MidiFilenameFormat> formats{MidiFilenameFormat::Absolute, MidiFilenameFormat::Roman, MidiFilenameFormat::Both};
    for (std::size_t i = 0; i < 3; ++i) CHECK(name(p, formats[i]) != before[i]);
    CHECK(name(p, MidiFilenameFormat::Both).find(chordLabel(p.key, r.chord, MidiFilenameFormat::Both)) != std::string::npos);
}

TEST_CASE("filename E: changing the tonic keeps the sounding chords and updates the numerals", "[filename]") {
    Progression p;
    p.key = Key{0, Mode::Major};
    ChordSpec c = chordAt(p.key, 8, Quality::Major, Seventh::Major);  // Abmaj7 = bVImaj7 in C
    p.blocks.push_back(ChordBlock{c, 0, 3840});
    CHECK(name(p, MidiFilenameFormat::Both, chordsOriginal(), true) == "Abmaj7 (bVImaj7).mid");
    // The same chord re-expressed in another key: Ab major 7 in Eb is IVmaj7; the absolute name is unchanged.
    Progression q = p;
    q.key = Key{3, Mode::Major};
    q.blocks[0].chord = withRootPitchClass(q.key, c, 8);
    CHECK(name(q, MidiFilenameFormat::Absolute, chordsOriginal(), true) == "Abmaj7.mid");
    CHECK(name(q, MidiFilenameFormat::Roman, chordsOriginal(), true) == "IVmaj7.mid");
    // A key change that keeps the chord's degree moves the absolute name and keeps the numeral.
    Progression r = p;
    r.key = Key{2, Mode::Major};
    r.blocks[0].chord = c;
    CHECK(name(r, MidiFilenameFormat::Roman, chordsOriginal(), true) == "bVImaj7.mid");
    CHECK(name(r, MidiFilenameFormat::Absolute, chordsOriginal(), true) == "Bbmaj7.mid");
}

TEST_CASE("filename F: chord density never changes the name", "[filename]") {
    auto p = fMinor();
    ProjectExportOptions o;
    o.part = ExportPart::Chords;
    const auto before = generateMidiFilename(p, MidiFilenameFormat::Both, o);
    p.voicing.density = 7;
    p.voicing.distribution = Distribution::Spread;
    p.blocks[2].voicing = p.voicing;
    CHECK(generateMidiFilename(p, MidiFilenameFormat::Both, o) == before);
}

TEST_CASE("filename G/H: chronological order, repeats kept, no insertion-order dependence", "[filename]") {
    Progression p;
    p.key = Key{5, Mode::Minor};
    const Tick bar = 3840;
    // stored out of order: Eb, Fm, Db, Fm  -> played Fm Fm Db Eb
    p.blocks.push_back(ChordBlock{chordAt(p.key, 10, Quality::Major), 3 * bar, bar});
    p.blocks.push_back(ChordBlock{chordAt(p.key, 0, Quality::Minor), 0, bar});
    p.blocks.push_back(ChordBlock{chordAt(p.key, 8, Quality::Major), 2 * bar, bar});
    p.blocks.push_back(ChordBlock{chordAt(p.key, 0, Quality::Minor), bar, bar});
    CHECK(name(p, MidiFilenameFormat::Absolute, chordsOriginal(), true) == "Fm - Fm - Db - Eb.mid");
    CHECK(name(p, MidiFilenameFormat::Roman, chordsOriginal(), true) == "i - i - bVI - bVII.mid");
    CHECK(name(p, MidiFilenameFormat::Both, chordsOriginal(), true) == "Fm (i) - Fm (i) - Db (bVI) - Eb (bVII).mid");
    // moving the last chord earlier than the first changes the order
    p.blocks[0].startTick = 0;
    p.blocks[1].startTick = bar / 2;  // Fm after Eb now, before the second Fm
    CHECK(name(p, MidiFilenameFormat::Absolute, chordsOriginal(), true).rfind("Eb - Fm - ", 0) == 0);
    // two chords at the same start keep their stored order (the existing stable policy)
    p.blocks[1].startTick = 0;
    CHECK(name(p, MidiFilenameFormat::Absolute, chordsOriginal(), true).rfind("Eb - Fm - ", 0) == 0);
}

TEST_CASE("filename: region and export type", "[filename]") {
    auto p = fMinor();
    ProjectExportOptions o = chordsOriginal();
    o.firstBar = 2;
    o.lastBar = 3;
    CHECK(name(p, MidiFilenameFormat::Absolute, o, true) == "Dbmaj7 - Abmaj9.mid");  // only the chords in bars 2-3
    o.firstBar = o.lastBar = 0;
    o.originalVoicing = false;
    CHECK(name(p, MidiFilenameFormat::Absolute, o, true) == "Fm9 - Dbmaj7 - Abmaj9 - Eb7_voiced.mid");
    o.part = ExportPart::Combined;
    CHECK(name(p, MidiFilenameFormat::Absolute, o, true) == "Fm9 - Dbmaj7 - Abmaj9 - Eb7_voiced_combined.mid");
    o.originalVoicing = true;
    CHECK(name(p, MidiFilenameFormat::Absolute, o, true) == "Fm9 - Dbmaj7 - Abmaj9 - Eb7_combined.mid");
    o.part = ExportPart::Melody;
    CHECK(name(p, MidiFilenameFormat::Both, o, true) == "Melody.mid");  // the existing melody convention
    Progression empty;
    ProjectExportOptions ch;
    ch.part = ExportPart::Chords;
    CHECK(generateMidiFilename(empty, MidiFilenameFormat::Both, ch, true) == "No chords_voiced.mid");
}

TEST_CASE("filename: slash chords stay Windows-safe", "[filename]") {
    Progression p;
    p.key = Key{0, Mode::Major};
    ChordSpec c;
    c.inversion = 1;  // C/E
    p.blocks.push_back(ChordBlock{c, 0, 3840});
    const auto n = name(p, MidiFilenameFormat::Absolute, chordsOriginal(), true);
    CHECK(n == "C_E.mid");
    CHECK(n.find('/') == std::string::npos);
}

TEST_CASE("filename I: the format persists and legacy projects default to Absolute", "[filename][serialization]") {
    const Progression legacy = makeDemoProgression();
    CHECK(legacy.exportNaming == MidiFilenameFormat::Absolute);
    CHECK(serializeProgression(legacy).find("exportNaming") == std::string::npos);
    for (auto f : {MidiFilenameFormat::Roman, MidiFilenameFormat::Both}) {
        Progression p = makeDemoProgression();
        p.exportNaming = f;
        const auto loaded = deserializeProgression(serializeProgression(p));
        REQUIRE(loaded.ok);
        CHECK(loaded.progression.exportNaming == f);
        CHECK(loaded.progression == p);
    }
}

TEST_CASE("filename J: the naming format never changes the MIDI notes", "[filename][export]") {
    auto p = fMinor();
    ProjectExportOptions o;
    o.part = ExportPart::Combined;
    std::vector<std::vector<std::uint8_t>> files;
    for (auto f : {MidiFilenameFormat::Absolute, MidiFilenameFormat::Roman, MidiFilenameFormat::Both}) {
        p.exportNaming = f;
        files.push_back(exportProject(p, o));
    }
    CHECK(files[0] == files[1]);
    CHECK(files[1] == files[2]);
    // and the notes are the playback notes, as before
    const auto file = smf::parse(files[0]);
    std::size_t noteCount = 0;
    for (const auto& t : file.tracks) noteCount += smf::extractNotes(t).size();
    CHECK(noteCount == generateProjectNotes(p).size());
}

TEST_CASE("filename: every export carries both progressions and the key as SMF text", "[filename][export]") {
    const auto p = fMinor();
    const auto lines = midiMetadataText(p);
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "Absolute Progression: Fm9 - Dbmaj7 - Abmaj9 - Eb7");
    CHECK(lines[1] == "Roman Progression: i9 - bVImaj7 - bIIImaj9 - bVII7");
    CHECK(lines[2] == "Project Key: F minor");
    ProjectExportOptions o;
    o.part = ExportPart::Chords;
    auto bytes = exportProject(p, o);
    const std::string blob(bytes.begin(), bytes.end());
    for (const auto& line : lines) CHECK(blob.find(line) != std::string::npos);
    o.part = ExportPart::Melody;
    const auto melody = exportProject(p, o);
    CHECK(std::string(melody.begin(), melody.end()).find("Absolute Progression") == std::string::npos);
}

TEST_CASE("filename K: long progressions are abbreviated at chord boundaries, safely", "[filename]") {
    Progression p;
    p.key = Key{0, Mode::Major};
    const Tick bar = 3840;
    for (int i = 0; i < 48; ++i) {
        ChordSpec c = chordAt(p.key, (i * 5) % 12, i % 2 ? Quality::Minor : Quality::Major, Seventh::Major, Ninth::Major);
        p.blocks.push_back(ChordBlock{c, i * bar, bar});
    }
    ProjectExportOptions o;
    o.part = ExportPart::Combined;
    for (bool ascii : {false, true}) {
        const auto n = generateMidiFilename(p, MidiFilenameFormat::Both, o, ascii);
        // length in UTF-16 units (everything here is in the BMP)
        std::size_t units = 0;
        for (unsigned char ch : n) if ((ch & 0xC0) != 0x80) ++units;
        CHECK(units <= static_cast<std::size_t>(kMaxFilenameUnits));
        CHECK(n.size() >= 4);
        CHECK(n.substr(n.size() - 4) == ".mid");
        CHECK(n.find("_voiced_combined.mid") != std::string::npos);  // suffix kept
        CHECK(n.find(" more [") != std::string::npos);
        // valid UTF-8, no broken characters
        for (std::size_t i = 0; i < n.size();) {
            const unsigned char c = static_cast<unsigned char>(n[i]);
            const std::size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
            REQUIRE(i + len <= n.size());
            for (std::size_t k = 1; k < len; ++k) REQUIRE((static_cast<unsigned char>(n[i + k]) & 0xC0) == 0x80);
            i += len;
        }
        // deterministic, and a different progression gets a different id
        CHECK(n == generateMidiFilename(p, MidiFilenameFormat::Both, o, ascii));
        auto q = p;
        q.blocks.back().chord = chordAt(q.key, 2, Quality::Minor);
        CHECK(n != generateMidiFilename(q, MidiFilenameFormat::Both, o, ascii));
    }
    // short names are untouched
    CHECK(name(fMinor(), MidiFilenameFormat::Absolute).find("more") == std::string::npos);
}
