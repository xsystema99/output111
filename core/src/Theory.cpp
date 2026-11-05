#include <chordcore/Theory.h>

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

bool ChordSpec::isValid() const noexcept {
    if (degree < 1 || degree > 7) return false;
    if (accidental < -1 || accidental > 1) return false;
    const int maxInversion = (seventh == Seventh::None) ? 2 : 3;
    if (ninth != Ninth::None && ((seventh != Seventh::Minor && seventh != Seventh::Major) || inversion != 0)) return false;
    return inversion >= 0 && inversion <= maxInversion;
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
    return intervals;
}

std::vector<int> voiceChord(const Key& key, const ChordSpec& chord, const VoicingOptions& voicing) {
    if (!key.isValid()) throw std::invalid_argument("invalid key");
    if (!chord.isValid()) throw std::invalid_argument("invalid chord");

    const int root = voicing.rootBase + chordRootPitchClass(key, chord);
    std::vector<int> pitches;
    for (int interval : chordIntervals(chord)) pitches.push_back(root + interval);

    // Each inversion step moves the current lowest note up an octave. Chord tones are less than
    // an octave apart, so the vector stays in ascending order.
    for (int step = 0; step < chord.inversion; ++step) {
        const int lowest = pitches.front();
        pitches.erase(pitches.begin());
        pitches.push_back(lowest + 12);
    }

    for (int p : pitches) {
        if (p < 0 || p > 127) throw std::out_of_range("chord pitch outside MIDI range 0..127");
    }
    return pitches;
}

}  // namespace chordcore
