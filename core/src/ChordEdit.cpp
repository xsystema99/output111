#include <chordcore/ChordEdit.h>

#include <algorithm>
#include <stdexcept>

namespace chordcore {

namespace {

constexpr Quality kMajorQualities[7] = {Quality::Major, Quality::Minor, Quality::Minor, Quality::Major,
                                        Quality::Major, Quality::Minor, Quality::Diminished};
constexpr Quality kMinorQualities[7] = {Quality::Minor, Quality::Diminished, Quality::Major, Quality::Minor,
                                        Quality::Minor, Quality::Major, Quality::Major};

constexpr Seventh kMajorSevenths[7] = {Seventh::Major, Seventh::Minor, Seventh::Minor, Seventh::Major,
                                       Seventh::Minor, Seventh::Minor, Seventh::Minor};
constexpr Seventh kMinorSevenths[7] = {Seventh::Minor, Seventh::Minor, Seventh::Major, Seventh::Minor,
                                       Seventh::Minor, Seventh::Major, Seventh::Minor};

void checkDegree(int degree) {
    if (degree < 1 || degree > 7) throw std::invalid_argument("scale degree must be 1..7");
}

ChordSpec clampInversion(ChordSpec chord) {
    chord.inversion = std::clamp(chord.inversion, 0, maxInversion(chord));
    return chord;
}

bool usesFlats(const Key& key) {
    const int relativeMajor = key.mode == Mode::Major ? key.tonic : (key.tonic + 3) % 12;
    switch (relativeMajor) {
        case 5: case 10: case 3: case 8: case 1: return true;  // F, Bb, Eb, Ab, Db
        default: return false;
    }
}

}  // namespace

Quality diatonicQuality(Mode mode, int degree) {
    checkDegree(degree);
    return (mode == Mode::Major ? kMajorQualities : kMinorQualities)[degree - 1];
}

Seventh diatonicSeventh(Mode mode, int degree) {
    checkDegree(degree);
    // The seventh of a diminished triad is the half-diminished one: a minor seventh above a diminished triad.
    return (mode == Mode::Major ? kMajorSevenths : kMinorSevenths)[degree - 1];
}

Ninth diatonicNinth(Mode mode, int degree) {
    checkDegree(degree);
    // The ninth is the scale note a step above the octave: a half step above the root's octave note is a flat 9.
    const int root = scaleDegreeSemitones(mode, degree);
    const int next = scaleDegreeSemitones(mode, degree % 7 + 1) + (degree == 7 ? 12 : 0);
    return next - root == 1 ? Ninth::Minor : Ninth::Major;
}

ChordSpec diatonicChord(Mode mode, int degree) {
    ChordSpec chord;
    chord.degree = degree;
    chord.quality = diatonicQuality(mode, degree);
    return chord;
}

bool isDiatonic(Mode mode, const ChordSpec& chord) {
    return chord.degree >= 1 && chord.degree <= 7 && chord.accidental == 0 &&
           chord.quality == diatonicQuality(mode, chord.degree);
}

ChordSpec withDegree(Mode mode, const ChordSpec& chord, int degree) {
    ChordSpec result = chord;
    result.degree = degree;
    result.accidental = 0;
    result.quality = diatonicQuality(mode, degree);
    if (chord.seventh != Seventh::None) result.seventh = diatonicSeventh(mode, degree);
    if (chord.ninth != Ninth::None) result.ninth = diatonicNinth(mode, degree);
    if (!canSuspend(result)) result.suspension = Suspension::None;  // vii° cannot keep a suspension
    return clampInversion(result);
}

ChordSpec asTriad(const ChordSpec& chord) {
    ChordSpec result = chord;
    result.seventh = Seventh::None;
    result.ninth = Ninth::None;
    result.suspension = Suspension::None;
    return clampInversion(result);
}

ChordSpec withSeventhToggled(Mode mode, const ChordSpec& chord) {
    ChordSpec result = chord;
    if (chord.ninth != Ninth::None) {  // a ninth chord drops back to its plain seventh chord
        result.ninth = Ninth::None;
        return clampInversion(result);
    }
    result.seventh = chord.seventh == Seventh::None ? diatonicSeventh(mode, chord.degree) : Seventh::None;
    return clampInversion(result);
}

ChordSpec withNinthToggled(Mode mode, const ChordSpec& chord) {
    ChordSpec result = chord;
    if (chord.ninth != Ninth::None) {
        result.ninth = Ninth::None;
        return clampInversion(result);
    }
    if (result.seventh == Seventh::None || result.seventh == Seventh::Diminished) result.seventh = diatonicSeventh(mode, chord.degree);
    result.ninth = diatonicNinth(mode, chord.degree);
    return clampInversion(result);
}

ChordSpec withInversion(const ChordSpec& chord, int inversion) {
    ChordSpec result = chord;
    result.inversion = inversion;
    return clampInversion(result);
}

bool canSuspend(const ChordSpec& chord) {
    return chord.quality == Quality::Major || chord.quality == Quality::Minor;
}

ChordSpec withSuspensionToggled(const ChordSpec& chord, Suspension suspension) {
    if (!canSuspend(chord)) return chord;
    ChordSpec result = chord;
    result.suspension = chord.suspension == suspension ? Suspension::None : suspension;
    return result;
}

void changeMode(Progression& progression, Mode newMode) {
    const Mode oldMode = progression.key.mode;
    if (oldMode == newMode) return;

    for (auto& block : progression.blocks) {
        ChordSpec& chord = block.chord;
        if (isDiatonic(oldMode, chord)) {
            if (chord.seventh != Seventh::None && chord.seventh == diatonicSeventh(oldMode, chord.degree)) {
                chord.seventh = diatonicSeventh(newMode, chord.degree);
            }
            if (chord.ninth != Ninth::None && chord.ninth == diatonicNinth(oldMode, chord.degree)) {
                chord.ninth = diatonicNinth(newMode, chord.degree);
            }
            chord.quality = diatonicQuality(newMode, chord.degree);
            if (!canSuspend(chord)) chord.suspension = Suspension::None;
            chord = clampInversion(chord);
        }
    }
    progression.key.mode = newMode;
}

namespace {
std::string spell(bool flats, int pitchClass) {
    static const char* const sharpNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    static const char* const flatNames[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    const int pc = ((pitchClass % 12) + 12) % 12;
    return flats ? flatNames[pc] : sharpNames[pc];
}
}  // namespace

std::string noteName(const Key& key, int pitchClass) { return spell(usesFlats(key), pitchClass); }

std::string chordName(const Key& key, const ChordSpec& chord) {
    if (!chord.isValid()) return "?";
    const int root = chordRootPitchClass(key, chord);
    // An explicitly flatted/sharped chord (bVII, #iv) is spelled that way whatever the key signature says.
    std::string name = chord.accidental < 0   ? spell(true, root)
                       : chord.accidental > 0 ? spell(false, root)
                                              : noteName(key, root);

    if (chord.ninth != Ninth::None) {
        // 9 / 7b9 for a minor seventh, maj9 / maj7b9 for a major seventh
        const bool flat = chord.ninth == Ninth::Minor;
        const bool major7 = chord.seventh == Seventh::Major;
        const std::string extFlat = std::string(major7 ? "maj" : "") + (flat ? "7b9" : "9");
        if (chord.suspension != Suspension::None) {
            name += extFlat + (chord.suspension == Suspension::Sus2 ? "sus2" : "sus4");
        } else {
            switch (chord.quality) {
                case Quality::Major: name += extFlat; break;
                case Quality::Minor: name += major7 ? "m(" + extFlat + ")" : "m" + extFlat; break;
                case Quality::Diminished: name += major7 ? "dim(" + extFlat + ")" : (flat ? "m7b5b9" : "m9b5"); break;
                case Quality::Augmented: name += "aug" + extFlat; break;
            }
        }
    } else if (chord.suspension != Suspension::None) {
        if (chord.seventh == Seventh::Minor) name += "7";
        else if (chord.seventh == Seventh::Major) name += "maj7";
        else if (chord.seventh == Seventh::Diminished) name += "dim7";
        name += chord.suspension == Suspension::Sus2 ? "sus2" : "sus4";
    } else {
        switch (chord.seventh) {
            case Seventh::None:
                switch (chord.quality) {
                    case Quality::Major: break;
                    case Quality::Minor: name += "m"; break;
                    case Quality::Diminished: name += "dim"; break;
                    case Quality::Augmented: name += "aug"; break;
                }
                break;
            case Seventh::Minor:
                switch (chord.quality) {
                    case Quality::Major: name += "7"; break;
                    case Quality::Minor: name += "m7"; break;
                    case Quality::Diminished: name += "m7b5"; break;
                    case Quality::Augmented: name += "aug7"; break;
                }
                break;
            case Seventh::Major:
                switch (chord.quality) {
                    case Quality::Major: name += "maj7"; break;
                    case Quality::Minor: name += "m(maj7)"; break;
                    case Quality::Diminished: name += "dim(maj7)"; break;
                    case Quality::Augmented: name += "augmaj7"; break;
                }
                break;
            case Seventh::Diminished:
                switch (chord.quality) {
                    case Quality::Major: name += "6"; break;
                    case Quality::Minor: name += "m6"; break;
                    case Quality::Diminished: name += "dim7"; break;
                    case Quality::Augmented: name += "aug6"; break;
                }
                break;
        }
    }

    if (chord.inversion > 0) {
        const auto intervals = chordIntervals(chord);
        name += "/" + noteName(key, root + intervals[static_cast<std::size_t>(chord.inversion)]);
    }
    return name;
}

}  // namespace chordcore
