#include <chordcore/Theory.h>

#include <algorithm>
#include <stdexcept>

namespace chordcore {

namespace {

constexpr int kMajorScale[7] = {0, 2, 4, 5, 7, 9, 11};
constexpr int kNaturalMinorScale[7] = {0, 2, 3, 5, 7, 8, 10};

int seventhInterval(Seventh s) {
    switch (s) {
        case Seventh::Minor: return 10;
        case Seventh::Major: return 11;
        case Seventh::Diminished: return 9;
        case Seventh::None: break;
    }
    return -1;
}

}  // namespace

int maxInversion(const ChordSpec& chord) noexcept {
    if (chord.eleventh != Eleventh::None || chord.thirteenth != Thirteenth::None) return 0;
    if (chord.ninth != Ninth::None) return 2;
    return chord.seventh == Seventh::None ? 2 : 3;
}

bool ChordSpec::isValid() const noexcept {
    if (degree < 1 || degree > 7) return false;
    if (accidental < -1 || accidental > 1) return false;
    const bool anyExtension = ninth != Ninth::None || eleventh != Eleventh::None || thirteenth != Thirteenth::None;
    if (anyExtension && seventh != Seventh::Minor && seventh != Seventh::Major) return false;
    if ((eleventh != Eleventh::None || thirteenth != Thirteenth::None) && ninth == Ninth::None) return false;
    if (eleventh == Eleventh::Perfect && thirteenth == Thirteenth::Major) return false;  // not expressible: "13" omits the 11
    return inversion >= 0 && inversion <= maxInversion(*this);
}

std::string extensionSymbol(const ChordSpec& chord) {
    if (chord.ninth == Ninth::None && chord.eleventh == Eleventh::None && chord.thirteenth == Thirteenth::None) return {};
    std::string top = "7";
    if (chord.thirteenth == Thirteenth::Major) top = "13";
    else if (chord.eleventh == Eleventh::Perfect) top = "11";
    else if (chord.ninth == Ninth::Major) top = "9";
    std::string text = chord.seventh == Seventh::Major ? "maj" : "";
    text += top;
    if (chord.ninth == Ninth::Minor) text += "b9";
    if (chord.ninth == Ninth::Sharp) text += "#9";
    if (chord.eleventh == Eleventh::Sharp) text += "#11";
    if (chord.thirteenth == Thirteenth::Minor) text += "b13";
    return text;
}

int scaleDegreeSemitones(Mode mode, int degree) {
    if (degree < 1 || degree > 7) throw std::invalid_argument("scale degree must be 1..7");
    return (mode == Mode::Major ? kMajorScale : kNaturalMinorScale)[degree - 1];
}

int chordRootPitchClass(const Key& key, const ChordSpec& chord) {
    const int semis = key.tonic + scaleDegreeSemitones(key.mode, chord.degree) + chord.accidental;
    return ((semis % 12) + 12) % 12;
}

std::vector<int> chordIntervals(const ChordSpec& chord) {
    int third = 4, fifth = 7;
    switch (chord.quality) {
        case Quality::Major: third = 4; fifth = 7; break;
        case Quality::Minor: third = 3; fifth = 7; break;
        case Quality::Diminished: third = 3; fifth = 6; break;
        case Quality::Augmented: third = 4; fifth = 8; break;
    }
    if (chord.suspension == Suspension::Sus2) third = 2;
    if (chord.suspension == Suspension::Sus4) third = 5;
    std::vector<int> intervals{0, third, fifth};
    if (chord.seventh != Seventh::None) intervals.push_back(seventhInterval(chord.seventh));
    if (chord.ninth == Ninth::Minor) intervals.push_back(13);
    if (chord.ninth == Ninth::Major) intervals.push_back(14);
    if (chord.ninth == Ninth::Sharp) intervals.push_back(15);
    if (chord.eleventh == Eleventh::Perfect) intervals.push_back(17);
    if (chord.eleventh == Eleventh::Sharp) intervals.push_back(18);
    if (chord.thirteenth == Thirteenth::Minor) intervals.push_back(20);
    if (chord.thirteenth == Thirteenth::Major) intervals.push_back(21);
    return intervals;
}

std::vector<int> voiceChord(const Key& key, const ChordSpec& chord, const VoicingOptions& voicing) {
    if (!key.isValid()) throw std::invalid_argument("invalid key");
    if (!chord.isValid()) throw std::invalid_argument("invalid chord");

    const int root = voicing.rootBase + chordRootPitchClass(key, chord);
    std::vector<int> pitches;
    for (int interval : chordIntervals(chord)) pitches.push_back(root + interval);

    // Each inversion step moves the current lowest note up an octave. Chord tones are less than
    // an octave apart (a ninth chord's top note may pass the moved root, so it is sorted afterwards).
    for (int step = 0; step < chord.inversion; ++step) {
        const int lowest = pitches.front();
        pitches.erase(pitches.begin());
        pitches.push_back(lowest + 12);
    }

    std::sort(pitches.begin(), pitches.end());
    for (int p : pitches) {
        if (p < 0 || p > 127) throw std::out_of_range("chord pitch outside MIDI range 0..127");
    }
    return pitches;
}

}  // namespace chordcore
