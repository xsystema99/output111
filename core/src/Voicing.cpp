#include <chordcore/Voicing.h>

#include <algorithm>
#include <cstdlib>
#include <numeric>
#include <stdexcept>

namespace chordcore {

namespace {

constexpr int kMinRangeSpan = 11;  // high - low

int floorMod12(int v) { return ((v % 12) + 12) % 12; }

// One chord tone of the original voicing.
struct Tone {
    int interval = 0;      // semitones above the root
    int index = 0;         // position in chordIntervals(): 0 root, 1 third/sus, 2 fifth, 3 seventh, 4.. extensions
    int basePitch = 0;     // pitch in the original voicing
    int keepScore = 0;     // priority when reducing
    int doubleScore = 0;   // priority when adding octave doublings
};

// One sounding voice while a voicing is being built.
struct Inst {
    int tone = 0;   // index into the tones vector
    int copy = 0;   // 0 = the tone itself, >= 1 = octave doublings
    int pitch = 0;
};

int keepScoreFor(const ChordSpec& chord, int index, int interval) {
    switch (index) {
        case 0: return 100;                                                   // root
        case 1: return 90;                                                    // third or suspended tone
        case 2: return (interval == 7) ? 30 : 70;                             // plain fifth is expendable, b5 / #5 defines the chord
        case 3: return (chord.seventh == Seventh::Major) ? 85 : 80;           // seventh, a major seventh is the signature
        default: break;
    }
    switch (interval) {
        case 13: case 15: case 18: case 20: return 65;  // alterations define the chord symbol
        case 21: return 62;                              // the 13th is characteristic ...
        case 14: return 50;                              // ... more than a plain 9th
        case 17: return 45;                              // and the 11th is the most expendable
        default: return 40;
    }
}

int doubleScoreFor(int index, int interval) {
    switch (index) {
        case 0: return 100;
        case 1: return 70;
        case 2: return (interval == 7) ? 80 : 25;
        case 3: return 45;
        default: return (interval == 13 || interval == 15 || interval == 18 || interval == 20) ? 20 : 50;
    }
}

}  // namespace

std::string validateVoicingSettings(const VoicingSettings& s) {
    if (s.density < 0 || s.density > kMaxDensity) return "density must be 0 (Auto) or 1..12";
    if (s.customLow < 0 || s.customHigh > 127 || s.customHigh - s.customLow < kMinRangeSpan) {
        return "custom register must stay within 0..127 and span at least an octave";
    }
    return {};
}

RegisterRange registerRange(const VoicingSettings& s, int octaveShift) {
    RegisterRange r;
    switch (s.registerPreset) {
        case RegisterPreset::Low: r = {24, 59}; break;
        case RegisterPreset::Mid: r = {36, 84}; break;
        case RegisterPreset::High: r = {60, 108}; break;
        case RegisterPreset::Full: return {0, 127};
        case RegisterPreset::Custom: return {s.customLow, s.customHigh};
    }
    r.low = std::clamp(r.low + 12 * octaveShift, 0, 127);
    r.high = std::clamp(r.high + 12 * octaveShift, 0, 127);
    if (r.high - r.low < kMinRangeSpan) {  // only possible at the extreme octave shifts
        if (r.low == 0) r.high = kMinRangeSpan;
        else r.low = r.high - kMinRangeSpan;
    }
    return r;
}

std::vector<int> VoicedChord::pitches() const {
    std::vector<int> out;
    out.reserve(notes.size());
    for (const auto& n : notes) out.push_back(n.pitch);
    return out;
}

const char* toString(RegisterPreset p) {
    switch (p) {
        case RegisterPreset::Low: return "low";
        case RegisterPreset::Mid: return "mid";
        case RegisterPreset::High: return "high";
        case RegisterPreset::Full: return "full";
        case RegisterPreset::Custom: return "custom";
    }
    return "mid";
}

const char* toString(Distribution d) {
    switch (d) {
        case Distribution::Automatic: return "automatic";
        case Distribution::Close: return "close";
        case Distribution::Open: return "open";
        case Distribution::Spread: return "spread";
    }
    return "automatic";
}

const char* toString(VoiceRole r) {
    switch (r) {
        case VoiceRole::Bass: return "bass";
        case VoiceRole::Lower: return "lower";
        case VoiceRole::Middle: return "middle";
        case VoiceRole::Upper: return "upper";
        case VoiceRole::Extension: return "extension";
    }
    return "middle";
}

namespace {

// Voice-leading cost in whole units; lower is smoother. Integer arithmetic only, so every compiler agrees.
int voiceLeadingCost(const std::vector<int>& cur, const std::vector<int>& prev) {
    if (cur.empty() || prev.empty()) return 0;
    int cost = 0;
    std::vector<int> assigned;
    for (int c : cur) {
        int best = 1000, bestIdx = 0;
        for (std::size_t i = 0; i < prev.size(); ++i) {
            const int d = std::abs(c - prev[i]);
            if (d < best) { best = d; bestIdx = static_cast<int>(i); }
        }
        cost += 2 * best;                       // movement
        if (best > 7) cost += 2 * (best - 7);   // leaps
        if (best == 0) cost -= 2;               // shared tone
        assigned.push_back(bestIdx);
    }
    for (std::size_t i = 0; i < assigned.size(); ++i) {  // voice crossing: nearest matches out of order
        for (std::size_t j = i + 1; j < assigned.size(); ++j) {
            if (assigned[i] > assigned[j]) cost += 6;
        }
    }
    const int sumCur = std::accumulate(cur.begin(), cur.end(), 0);
    const int sumPrev = std::accumulate(prev.begin(), prev.end(), 0);
    const int nCur = static_cast<int>(cur.size()), nPrev = static_cast<int>(prev.size());
    cost += std::abs(sumCur * nPrev - sumPrev * nCur) / (nCur * nPrev);  // register continuity (mean pitch), x1
    cost += std::abs(cur.front() - prev.front());                        // bass continuity
    return cost;
}

}  // namespace

VoicedChord voiceChordWithSettings(const Key& key, const ChordSpec& chord, const VoicingOptions& voicing,
                                   const VoicingSettings& settings, int octaveShift,
                                   const std::vector<int>* previousPitches) {
    if (!key.isValid()) throw std::invalid_argument("invalid key");
    if (!chord.isValid()) throw std::invalid_argument("invalid chord");
    {
        const std::string problem = validateVoicingSettings(settings);
        if (!problem.empty()) throw std::invalid_argument(problem);
    }

    // --- The original voicing, with each pitch tied back to its chord tone. -------------------------------------
    const int rootPitch = voicing.rootBase + chordRootPitchClass(key, chord);
    const std::vector<int> intervals = chordIntervals(chord);
    std::vector<Tone> tones;
    for (std::size_t i = 0; i < intervals.size(); ++i) {
        Tone t;
        t.interval = intervals[i];
        t.index = static_cast<int>(i);
        t.basePitch = rootPitch + intervals[i];
        t.keepScore = keepScoreFor(chord, t.index, t.interval);
        t.doubleScore = doubleScoreFor(t.index, t.interval);
        tones.push_back(t);
    }
    for (int step = 0; step < chord.inversion; ++step) {  // identical to voiceChord(): lowest tone moves up an octave
        auto lowest = std::min_element(tones.begin(), tones.end(), [](const Tone& a, const Tone& b) { return a.basePitch < b.basePitch; });
        lowest->basePitch += 12;
    }
    const std::vector<int> originalPitches = voiceChord(key, chord, voicing);  // also range-checks 0..127

    VoicedChord result;
    result.range = registerRange(settings, octaveShift);
    result.originalVoices = static_cast<int>(originalPitches.size());

    const int toneCount = static_cast<int>(tones.size());
    int bassTone = 0;  // the tone that is lowest in the original voicing
    for (int i = 1; i < toneCount; ++i) {
        if (tones[static_cast<std::size_t>(i)].basePitch < tones[static_cast<std::size_t>(bassTone)].basePitch) bassTone = i;
    }

    auto finish = [&](std::vector<Inst>& voices) {
        std::sort(voices.begin(), voices.end(), [](const Inst& a, const Inst& b) { return a.pitch < b.pitch; });
        // Roles: the lowest voice is the bass, extension tones are extensions, the rest split into lower / middle / upper.
        std::vector<std::size_t> plain;
        for (std::size_t i = 0; i < voices.size(); ++i) {
            const Tone& t = tones[static_cast<std::size_t>(voices[i].tone)];
            VoicedNote n;
            n.pitch = voices[i].pitch;
            n.interval = t.interval;
            n.voiceId = t.interval * 8 + std::min(voices[i].copy, 7);
            n.doubling = voices[i].copy > 0;
            n.original = std::binary_search(originalPitches.begin(), originalPitches.end(), n.pitch);
            if (i == 0) n.role = VoiceRole::Bass;
            else if (t.index >= 4) n.role = VoiceRole::Extension;
            else plain.push_back(i);
            result.notes.push_back(n);
        }
        const int k = static_cast<int>(plain.size());
        for (int r = 0; r < k; ++r) {
            VoiceRole role = VoiceRole::Middle;
            if (r * 3 < k) role = VoiceRole::Lower;
            else if (r * 3 >= 2 * k) role = VoiceRole::Upper;
            result.notes[plain[static_cast<std::size_t>(r)]].role = role;
        }
        result.actualVoices = static_cast<int>(result.notes.size());
    };

    // --- Auto / Original ------------------------------------------------------------------------------------------
    if (settings.isAuto()) {
        std::vector<Inst> voices;
        for (int i = 0; i < toneCount; ++i) voices.push_back({i, 0, tones[static_cast<std::size_t>(i)].basePitch});
        result.original = true;
        result.targetVoices = result.originalVoices;
        finish(voices);
        return result;
    }
    result.original = false;
    result.targetVoices = settings.density;
    const int target = settings.density;
    const int lo = result.range.low, hi = result.range.high;

    // --- Choose which chord tones sound (reduction) and which are doubled (expansion). ----------------------------
    std::vector<int> byKeep(static_cast<std::size_t>(toneCount));
    std::iota(byKeep.begin(), byKeep.end(), 0);
    auto keepScore = [&](int i) {
        const Tone& t = tones[static_cast<std::size_t>(i)];
        return (settings.preserveBass && i == bassTone) ? 110 : t.keepScore;  // an explicit bass outranks everything
    };
    std::stable_sort(byKeep.begin(), byKeep.end(), [&](int a, int b) { return keepScore(a) > keepScore(b); });

    std::vector<Inst> voices;
    std::vector<int> selected;
    for (int i = 0; i < std::min(target, toneCount); ++i) selected.push_back(byKeep[static_cast<std::size_t>(i)]);
    std::sort(selected.begin(), selected.end());  // keep chord order for deterministic layout
    for (int i : selected) voices.push_back({i, 0, tones[static_cast<std::size_t>(i)].basePitch});

    std::vector<int> byDouble = selected;
    std::stable_sort(byDouble.begin(), byDouble.end(), [&](int a, int b) {
        return tones[static_cast<std::size_t>(a)].doubleScore > tones[static_cast<std::size_t>(b)].doubleScore;
    });
    std::vector<int> nextCopy(static_cast<std::size_t>(toneCount), 1);
    for (int extra = 0; extra < target - toneCount; ++extra) {
        const int t = byDouble[static_cast<std::size_t>(extra) % byDouble.size()];
        voices.push_back({t, nextCopy[static_cast<std::size_t>(t)]++, 0});
    }

    // The tone the layout is built upward from.
    int basis = -1;
    if (settings.preserveBass) {
        basis = bassTone;
    } else {
        for (int i : selected) {
            if (basis < 0 || tones[static_cast<std::size_t>(i)].index < tones[static_cast<std::size_t>(basis)].index) basis = i;
        }
    }
    const Tone& basisTone = tones[static_cast<std::size_t>(basis)];
    const int bassPitch = basisTone.basePitch;

    // --- Layout by distribution. ---------------------------------------------------------------------------------
    std::vector<bool> taken(128, false);
    auto isTaken = [&](int pitch) { return pitch >= 0 && pitch <= 127 && taken[static_cast<std::size_t>(pitch)]; };
    auto mark = [&](int pitch, bool value) {
        if (pitch >= 0 && pitch <= 127) taken[static_cast<std::size_t>(pitch)] = value;
    };
    auto place = [&](Inst& v, int pitch) {  // distinct pitches: bump by octaves until free (the register fit follows)
        while (isTaken(pitch)) pitch += 12;
        v.pitch = pitch;
        mark(pitch, true);
    };
    auto closeOffset = [&](const Inst& v) {
        const Tone& t = tones[static_cast<std::size_t>(v.tone)];
        return floorMod12(t.interval - basisTone.interval) + 12 * v.copy;
    };

    // Process the original (copy 0) voices first, then doublings, each group in a fixed order.
    std::vector<std::size_t> order(voices.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (voices[a].copy != voices[b].copy) return voices[a].copy < voices[b].copy;
        return tones[static_cast<std::size_t>(voices[a].tone)].index < tones[static_cast<std::size_t>(voices[b].tone)].index;
    });

    if (settings.distribution == Distribution::Automatic) {
        for (std::size_t k : order) {
            Inst& v = voices[k];
            const Tone& t = tones[static_cast<std::size_t>(v.tone)];
            int p = t.basePitch;
            if (v.copy > 0) {
                const bool bassDoubling = (v.tone == basis) && v.copy == 1;
                p = bassDoubling ? t.basePitch - 12 : t.basePitch + 12 * (v.tone == basis ? v.copy - 1 : v.copy);
            }
            place(v, p);
        }
    } else {
        // Close stack above the basis tone, optionally opened or spread.
        std::vector<std::size_t> uppers;
        for (std::size_t k : order) {
            Inst& v = voices[k];
            place(v, bassPitch + closeOffset(v));
            if (!(v.tone == basis && v.copy == 0)) uppers.push_back(k);
        }
        if (settings.distribution != Distribution::Close) {
            std::sort(uppers.begin(), uppers.end(), [&](std::size_t a, std::size_t b) { return voices[a].pitch < voices[b].pitch; });
            std::fill(taken.begin(), taken.end(), false);
            for (const auto& v : voices) mark(v.pitch, true);
            if (uppers.size() >= 2) {
                for (std::size_t j = 0; j < uppers.size(); ++j) {
                    Inst& v = voices[uppers[j]];
                    int lift = 0;
                    if (settings.distribution == Distribution::Open) lift = (j % 2 == 0) ? 12 : 0;
                    else lift = 12 * static_cast<int>((j + 1) / 2);
                    if (lift == 0) continue;
                    mark(v.pitch, false);
                    place(v, v.pitch + lift);
                }
            }
        }
    }

    // --- Register: shift the whole voicing by octaves to fit, then fold stragglers, drop what cannot fit. ---------
    {
        int bestShift = 0, bestOutside = 1 << 20;
        for (int k = -8; k <= 8; ++k) {
            int outside = 0;
            for (const auto& v : voices) {
                const int p = v.pitch + 12 * k;
                if (p < lo || p > hi) ++outside;
            }
            if (outside < bestOutside || (outside == bestOutside && std::abs(k) < std::abs(bestShift))) {
                bestOutside = outside;
                bestShift = k;
            }
        }
        for (auto& v : voices) v.pitch += 12 * bestShift;
    }
    auto fit = [&](std::vector<Inst>& vs) {
        std::fill(taken.begin(), taken.end(), false);
        std::vector<std::size_t> ord(vs.size());
        std::iota(ord.begin(), ord.end(), std::size_t{0});
        std::stable_sort(ord.begin(), ord.end(), [&](std::size_t a, std::size_t b) {
            const bool ba = vs[a].tone == basis, bb = vs[b].tone == basis;  // the bass tone (and its doublings) first
            if (ba != bb) return ba;
            return vs[a].pitch < vs[b].pitch;
        });
        std::vector<Inst> kept;
        int floorPitch = -1;
        for (std::size_t k : ord) {
            Inst v = vs[k];
            const bool isBass = v.tone == basis;
            int p = v.pitch;
            while (p > hi) p -= 12;
            while (p < lo) p += 12;
            if (isBass) {
                if (floorPitch < 0) floorPitch = p;  // the lowest bass-tone voice is the floor for everything else
            } else if (settings.preserveBass) {
                while (p <= floorPitch) p += 12;
            }
            while (p <= hi && taken[static_cast<std::size_t>(p)]) p += 12;
            if (p > hi || p < lo) continue;  // cannot be placed inside the register
            v.pitch = p;
            taken[static_cast<std::size_t>(p)] = true;
            kept.push_back(v);
        }
        vs = kept;
    };
    fit(voices);

    // --- Top up with octave doublings when folding lost voices (chord tones only, never scale notes). ------------
    {
        int guard = 0;
        std::size_t cursor = 0;
        int stale = 0;
        const int floorPitch = voices.empty() ? lo : voices.front().pitch;
        while (static_cast<int>(voices.size()) < target && guard++ < 256 && stale < static_cast<int>(byDouble.size())) {
            const int t = byDouble[cursor++ % byDouble.size()];
            const int pc = floorMod12(tones[static_cast<std::size_t>(t)].basePitch);
            int sum = 0;
            for (const auto& v : voices) sum += v.pitch;
            const int mean = voices.empty() ? (lo + hi) / 2 : sum / static_cast<int>(voices.size());
            int bestPitch = -1;
            for (int p = lo; p <= hi; ++p) {
                if (floorMod12(p) != pc || taken[static_cast<std::size_t>(p)]) continue;
                if (settings.preserveBass && p <= floorPitch) continue;
                if (bestPitch < 0 || std::abs(p - mean) < std::abs(bestPitch - mean)) bestPitch = p;
            }
            if (bestPitch < 0) { ++stale; continue; }
            stale = 0;
            voices.push_back({t, nextCopy[static_cast<std::size_t>(t)]++, bestPitch});
            taken[static_cast<std::size_t>(bestPitch)] = true;
        }
    }

    // --- Voice leading: choose the octave placement closest to the previous chord. -------------------------------
    if (settings.voiceLeading && previousPitches != nullptr && !previousPitches->empty() && !voices.empty()) {
        std::sort(voices.begin(), voices.end(), [](const Inst& a, const Inst& b) { return a.pitch < b.pitch; });
        std::vector<Inst> best = voices;
        auto pitchesOf = [](const std::vector<Inst>& vs) {
            std::vector<int> out;
            for (const auto& v : vs) out.push_back(v.pitch);
            return out;
        };
        int bestCost = voiceLeadingCost(pitchesOf(voices), *previousPitches);
        const int maxRotation = settings.preserveBass ? 0 : static_cast<int>(voices.size()) - 1;
        std::vector<Inst> rotated = voices;
        for (int r = 0; r <= maxRotation; ++r) {
            if (r > 0) {  // another inversion: the lowest voice moves up an octave
                std::sort(rotated.begin(), rotated.end(), [](const Inst& a, const Inst& b) { return a.pitch < b.pitch; });
                rotated.front().pitch += 12;
                bool clash = false;
                for (std::size_t i = 1; i < rotated.size(); ++i) clash = clash || rotated[i].pitch == rotated.front().pitch;
                if (rotated.front().pitch > hi || clash) break;
            }
            for (int shift : {0, -12, 12, -24, 24}) {
                std::vector<Inst> cand = rotated;
                bool inside = true;
                for (auto& v : cand) {
                    v.pitch += shift;
                    inside = inside && v.pitch >= lo && v.pitch <= hi;
                }
                if (!inside || (r == 0 && shift == 0)) continue;
                const int c = voiceLeadingCost(pitchesOf(cand), *previousPitches);
                if (c < bestCost) { bestCost = c; best = cand; }
            }
            if (r > 0) {  // the unshifted rotation itself
                const int c = voiceLeadingCost(pitchesOf(rotated), *previousPitches);
                if (c < bestCost) { bestCost = c; best = rotated; }
            }
        }
        voices = best;
    }

    finish(voices);
    if (result.actualVoices < target) {
        result.note = "the register has room for only " + std::to_string(result.actualVoices) + " distinct chord-tone voices";
    }
    return result;
}

}  // namespace chordcore
