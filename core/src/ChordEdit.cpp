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

namespace {
/// Semitones from the chord root up to the scale note `steps` scale steps above it (steps 1 = 9th, 3 = 11th,
/// 5 = 13th), counted in the octave above the root (minus one octave).
int scaleStepInterval(Mode mode, int degree, int steps) {
    const int root = scaleDegreeSemitones(mode, degree);
    const int index = degree - 1 + steps;
    const int note = scaleDegreeSemitones(mode, index % 7 + 1) + 12 * (index / 7);
    return note - root;
}
}  // namespace

Eleventh diatonicEleventh(Mode mode, int degree) {
    checkDegree(degree);
    return scaleStepInterval(mode, degree, 3) == 6 ? Eleventh::Sharp : Eleventh::Perfect;
}

Thirteenth diatonicThirteenth(Mode mode, int degree) {
    checkDegree(degree);
    return scaleStepInterval(mode, degree, 5) == 8 ? Thirteenth::Minor : Thirteenth::Major;
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
    // An extension that is the old degree's diatonic one follows the new degree; an altered one (#9, b13, ...) stays.
    if (chord.ninth != Ninth::None && chord.ninth == diatonicNinth(mode, chord.degree)) result.ninth = diatonicNinth(mode, degree);
    if (chord.eleventh != Eleventh::None && chord.eleventh == diatonicEleventh(mode, chord.degree)) result.eleventh = diatonicEleventh(mode, degree);
    if (chord.thirteenth != Thirteenth::None && chord.thirteenth == diatonicThirteenth(mode, chord.degree)) result.thirteenth = diatonicThirteenth(mode, degree);
    if (!canSuspend(result)) result.suspension = Suspension::None;  // vii° cannot keep a suspension
    return clampInversion(result);
}

ChordSpec asTriad(const ChordSpec& chord) {
    ChordSpec result = chord;
    result.seventh = Seventh::None;
    result.ninth = Ninth::None;
    result.eleventh = Eleventh::None;
    result.thirteenth = Thirteenth::None;
    result.suspension = Suspension::None;
    return clampInversion(result);
}

ChordSpec withSeventhToggled(Mode mode, const ChordSpec& chord) {
    ChordSpec result = chord;
    if (chord.ninth != Ninth::None) {  // an extended chord drops back to its plain seventh chord
        result.ninth = Ninth::None;
        result.eleventh = Eleventh::None;
        result.thirteenth = Thirteenth::None;
        return clampInversion(result);
    }
    result.seventh = chord.seventh == Seventh::None ? diatonicSeventh(mode, chord.degree) : Seventh::None;
    return clampInversion(result);
}

ChordSpec withNinthToggled(Mode mode, const ChordSpec& chord) {
    ChordSpec result = chord;
    if (chord.ninth != Ninth::None) {  // the 9th and everything stacked above it
        result.ninth = Ninth::None;
        result.eleventh = Eleventh::None;
        result.thirteenth = Thirteenth::None;
        return clampInversion(result);
    }
    if (result.seventh == Seventh::None || result.seventh == Seventh::Diminished) result.seventh = diatonicSeventh(mode, chord.degree);
    result.ninth = diatonicNinth(mode, chord.degree);
    return clampInversion(result);
}

namespace {
/// Seventh and ninth in place, as the base for an 11th, 13th or alteration.
ChordSpec withNinthBase(Mode mode, ChordSpec chord) {
    if (chord.seventh == Seventh::None || chord.seventh == Seventh::Diminished) chord.seventh = diatonicSeventh(mode, chord.degree);
    if (chord.ninth == Ninth::None) chord.ninth = diatonicNinth(mode, chord.degree);
    return chord;
}
}  // namespace

ChordSpec withEleventhToggled(Mode mode, const ChordSpec& chord) {
    if (chord.eleventh != Eleventh::None) {
        ChordSpec result = chord;
        result.eleventh = Eleventh::None;
        return clampInversion(result);
    }
    ChordSpec result = withNinthBase(mode, chord);
    result.eleventh = diatonicEleventh(mode, chord.degree);
    if (result.eleventh == Eleventh::Perfect && result.thirteenth == Thirteenth::Major) result.thirteenth = Thirteenth::None;
    return clampInversion(result);
}

ChordSpec withThirteenthToggled(Mode mode, const ChordSpec& chord) {
    if (chord.thirteenth != Thirteenth::None) {
        ChordSpec result = chord;
        result.thirteenth = Thirteenth::None;
        return clampInversion(result);
    }
    ChordSpec result = withNinthBase(mode, chord);
    result.thirteenth = diatonicThirteenth(mode, chord.degree);
    if (result.thirteenth == Thirteenth::Major && result.eleventh == Eleventh::Perfect) result.eleventh = Eleventh::None;
    return clampInversion(result);
}

ChordSpec withAlterationToggled(Mode mode, const ChordSpec& chord, Alteration alteration) {
    ChordSpec result = withNinthBase(mode, chord);
    switch (alteration) {
        case Alteration::FlatNine: result.ninth = result.ninth == Ninth::Minor ? Ninth::Major : Ninth::Minor; break;
        case Alteration::SharpNine: result.ninth = result.ninth == Ninth::Sharp ? Ninth::Major : Ninth::Sharp; break;
        case Alteration::SharpEleven:
            result.eleventh = result.eleventh == Eleventh::Sharp ? Eleventh::None : Eleventh::Sharp;
            break;
        case Alteration::FlatThirteen:
            result.thirteenth = result.thirteenth == Thirteenth::Minor ? Thirteenth::None : Thirteenth::Minor;
            break;
    }
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
            if (chord.eleventh != Eleventh::None && chord.eleventh == diatonicEleventh(oldMode, chord.degree)) {
                chord.eleventh = diatonicEleventh(newMode, chord.degree);
            }
            if (chord.thirteenth != Thirteenth::None && chord.thirteenth == diatonicThirteenth(oldMode, chord.degree)) {
                chord.thirteenth = diatonicThirteenth(newMode, chord.degree);
            }
            if (chord.eleventh == Eleventh::Perfect && chord.thirteenth == Thirteenth::Major) chord.thirteenth = Thirteenth::None;
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
        const std::string ext = extensionSymbol(chord);  // e.g. 9, maj9, 7b9, 13#11
        const bool major7 = chord.seventh == Seventh::Major;
        if (chord.suspension != Suspension::None) {
            name += ext + (chord.suspension == Suspension::Sus2 ? "sus2" : "sus4");
        } else {
            switch (chord.quality) {
                case Quality::Major: name += ext; break;
                case Quality::Minor: name += major7 ? "m(" + ext + ")" : "m" + ext; break;
                case Quality::Diminished:
                    if (major7) {
                        name += "dim(" + ext + ")";
                    } else {  // half-diminished: the b5 goes after the number, before the alterations: m9b5, m7b5b9
                        const auto split = ext.find_first_of("b#");
                        name += "m" + ext.substr(0, split) + "b5" + (split == std::string::npos ? "" : ext.substr(split));
                    }
                    break;
                case Quality::Augmented: name += "aug" + ext; break;
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

bool spellRoot(const Key& key, int rootRel, int& degreeOut, int& accidentalOut) {
    rootRel = ((rootRel % 12) + 12) % 12;
    int bestDegree = 0, bestAcc = 0, bestCost = 99;
    for (int degree = 1; degree <= 7; ++degree) {
        int d = rootRel - scaleDegreeSemitones(key.mode, degree);
        if (d > 6) d -= 12;
        if (d < -6) d += 12;
        if (d < -1 || d > 1) continue;
        int cost = (d < 0 ? -d : d) * 10 + (d > 0 ? 1 : 0);  // exact first, then flat before sharp
        if (rootRel == 6 && d == 1) cost = 5;                 // the tritone reads as #iv
        if (cost < bestCost) { bestCost = cost; bestDegree = degree; bestAcc = d; }
    }
    if (bestDegree == 0) return false;
    degreeOut = bestDegree;
    accidentalOut = bestAcc;
    return true;
}

ChordSpec withRootPitchClass(const Key& key, const ChordSpec& chord, int pitchClass) {
    ChordSpec out = chord;
    if (!spellRoot(key, pitchClass - key.tonic, out.degree, out.accidental)) return chord;
    out.inversion = std::min(out.inversion, maxInversion(out));
    return out.isValid() ? out : chord;
}

ChordSpec withQuality(const ChordSpec& chord, Quality quality) {
    ChordSpec out = chord;
    out.quality = quality;
    if (!canSuspend(out)) out.suspension = Suspension::None;
    out.inversion = std::min(out.inversion, maxInversion(out));
    return out.isValid() ? out : chord;
}

ChordSpec withSeventhKind(const ChordSpec& chord, Seventh seventh) {
    ChordSpec out = chord;
    out.seventh = seventh;
    if (seventh != Seventh::Minor && seventh != Seventh::Major) {
        out.ninth = Ninth::None;
        out.eleventh = Eleventh::None;
        out.thirteenth = Thirteenth::None;
    }
    out.inversion = std::min(out.inversion, maxInversion(out));
    return out.isValid() ? out : chord;
}

}  // namespace chordcore
