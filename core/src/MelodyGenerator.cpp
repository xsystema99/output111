#include <chordcore/MelodyGenerator.h>

#include <algorithm>
#include <cmath>
#include <string>

#include <chordcore/Grid.h>
#include <chordcore/Harmony.h>
#include <chordcore/MelodyEdit.h>

namespace chordcore {

namespace {

constexpr double kPi = 3.14159265358979323846;

// SplitMix64: tiny, fast, identical everywhere.
struct Rng {
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double unit() { return static_cast<double>(next() >> 11) / 9007199254740992.0; }  // [0,1)
    int below(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<std::uint64_t>(n)); }
};

int mod12(int v) { return ((v % 12) + 12) % 12; }

OpResult failure(std::string message) {
    OpResult r;
    r.error = std::move(message);
    return r;
}

struct Slot {
    int index;       // within the bar
    int strength;    // 3 downbeat, 2 mid-bar beat, 1 other beat, 0 off-beat
};

double shape(Contour c, double u) {
    switch (c) {
        case Contour::Ascending: return u;
        case Contour::Descending: return 1.0 - u;
        case Contour::Arch: return std::sin(kPi * u);
        case Contour::InvertedArch: return 1.0 - std::sin(kPi * u);
        case Contour::Wave: return 0.5 + 0.5 * std::sin(2.0 * kPi * 2.0 * u);
        case Contour::Free: return 0.5;
    }
    return 0.5;
}

}  // namespace

std::uint32_t chordHash(const ChordSpec& c) {
    std::uint32_t h = 2166136261u;
    auto mix = [&h](int v) {
        h ^= static_cast<std::uint32_t>(v + 1000);
        h *= 16777619u;
    };
    mix(c.degree);
    mix(c.accidental);
    mix(static_cast<int>(c.quality));
    mix(static_cast<int>(c.seventh));
    mix(c.inversion);
    mix(static_cast<int>(c.suspension));
    mix(static_cast<int>(c.ninth));
    mix(static_cast<int>(c.eleventh));
    mix(static_cast<int>(c.thirteenth));
    return h;
}

GenerationRange scopeRange(const Progression& p, PhraseScope scope, int startBar, Tick selectedStart, Tick selectedEnd) {
    const Tick bar = p.timeSignature.ticksPerBar();
    const Tick limit = maxEndTick(p.timeSignature);
    const Tick projectEnd = std::max(p.lengthTicks(), bar * 4);
    GenerationRange r;
    auto bars = [&](int n) {
        r.start = static_cast<Tick>(std::max(1, startBar) - 1) * bar;
        r.end = r.start + static_cast<Tick>(n) * bar;
    };
    switch (scope) {
        case PhraseScope::OneBar: bars(1); break;
        case PhraseScope::TwoBars: bars(2); break;
        case PhraseScope::FourBars: bars(4); break;
        case PhraseScope::EightBars: bars(8); break;
        case PhraseScope::FullProgression:
            r.start = 0;
            r.end = projectEnd;
            break;
        case PhraseScope::SelectedRegion:
            if (selectedEnd > selectedStart) {
                r.start = std::max<Tick>(selectedStart, 0);
                r.end = selectedEnd;
            } else {
                bars(4);
            }
            break;
    }
    r.end = std::min(r.end, limit);
    if (r.start > r.end) r.start = r.end;
    return r;
}

std::vector<MelodyNote> generateMelodyForRange(const Progression& p, const GeneratorSettings& g, GenerationRange range) {
    std::vector<MelodyNote> out;
    const Tick limit = maxEndTick(p.timeSignature);
    range.start = std::max<Tick>(range.start, 0);
    range.end = std::min(range.end, limit);
    if (range.end <= range.start) return out;

    const Tick bar = p.timeSignature.ticksPerBar();
    const Tick beat = beatTicks(p.timeSignature);
    const Tick slotTicks = std::max<Tick>(beat / 2, 1);
    const int slotsPerBar = static_cast<int>(std::max<Tick>(bar / slotTicks, 1));
    const int slotsPerBeat = static_cast<int>(std::max<Tick>(beat / slotTicks, 1));

    const int low = std::clamp(g.registerLow, 0, 127);
    const int high = std::clamp(g.registerHigh, low, 127);
    const double tension = std::clamp(g.tension, 0, 100) / 100.0;

    // Phrase length in bars (for contour and phrase endings).
    int phraseBars = 4;
    switch (g.scope) {
        case PhraseScope::OneBar: phraseBars = 1; break;
        case PhraseScope::TwoBars: phraseBars = 2; break;
        case PhraseScope::FourBars: phraseBars = 4; break;
        case PhraseScope::EightBars: phraseBars = 8; break;
        default: phraseBars = 4; break;
    }
    const Tick phraseTicks = static_cast<Tick>(phraseBars) * bar;

    const auto regions = buildCompatibility(p);
    const std::uint16_t scaleMask = scalePitchClassMask(p.key);

    // Existing notes overlapping the range are obstacles (time) and context (pitch).
    std::vector<const MelodyNote*> kept;
    for (const auto& n : p.melody) {
        if (n.endTick() > range.start && n.startTick < range.end) kept.push_back(&n);
    }
    auto coveredAt = [&](Tick t) {
        for (auto* n : kept) {
            if (n->startTick <= t && t < n->endTick()) return true;
        }
        return false;
    };
    auto nextObstacleStart = [&](Tick t) {
        Tick best = limit;
        for (auto* n : kept) {
            if (n->startTick > t) best = std::min(best, n->startTick);
        }
        return best;
    };
    auto previousKept = [&](Tick t, int& pitch) {
        Tick bestStart = -1;
        for (auto* n : kept) {
            if (n->startTick < t && n->startTick > bestStart) {
                bestStart = n->startTick;
                pitch = n->pitch;
            }
        }
        return bestStart >= 0;
    };

    Rng rng(static_cast<std::uint64_t>(g.seed) * 0x9E3779B97F4A7C15ull ^ (static_cast<std::uint64_t>(range.start) + 1) * 0xD6E8FEB86659FD93ull);

    // ---- rhythm: onsets per bar -------------------------------------------------------------------------
    struct Onset {
        Tick tick;
        Tick length;
        int strength;
        bool phraseEnd;
        int barIndexInRange;
    };
    std::vector<Onset> onsets;

    auto strengthOf = [&](int slot) {
        if (slot == 0) return 3;
        if (slot % slotsPerBeat != 0) return 0;
        const int beatIndex = slot / slotsPerBeat;
        const int beatsPerBar = std::max(1, slotsPerBar / slotsPerBeat);
        return (beatsPerBar % 2 == 0 && beatIndex == beatsPerBar / 2) ? 2 : 1;
    };

    double repeatProbability = 0.5;
    int minCount = 4, maxCount = 5;
    switch (g.density) {
        case Density::Sparse: minCount = 2; maxCount = 3; break;
        case Density::Moderate: minCount = 4; maxCount = 5; break;
        case Density::Dense: minCount = 6; maxCount = 8; break;
    }
    switch (g.rhythm) {
        case RhythmStyle::Sustained: repeatProbability = 0.3; break;
        case RhythmStyle::Lyrical: repeatProbability = 0.5; break;
        case RhythmStyle::Rhythmic: repeatProbability = 0.75; break;
        case RhythmStyle::Syncopated: repeatProbability = 0.6; break;
        case RhythmStyle::Free: repeatProbability = 0.15; break;
    }

    std::vector<int> previousPattern;
    const Tick firstBarStart = (range.start / bar) * bar;
    int barNumber = 0;
    for (Tick barStart = firstBarStart; barStart < range.end; barStart += bar, ++barNumber) {
        std::vector<int> pattern;
        if (!previousPattern.empty() && rng.unit() < repeatProbability) {
            pattern = previousPattern;  // motif repetition
        } else {
            // Weights per slot for this style.
            std::vector<double> weight(static_cast<std::size_t>(slotsPerBar));
            for (int s = 0; s < slotsPerBar; ++s) {
                const bool onBeat = s % slotsPerBeat == 0;
                double w = 5.0;
                switch (g.rhythm) {
                    case RhythmStyle::Sustained: w = onBeat ? (s == 0 ? 12.0 : 5.0) : 0.0; break;
                    case RhythmStyle::Lyrical: w = onBeat ? (s == 0 ? 10.0 : 8.0) : 3.0; break;
                    case RhythmStyle::Rhythmic: w = onBeat ? (s == 0 ? 10.0 : 6.0) : 6.0; break;
                    case RhythmStyle::Syncopated: w = onBeat ? (s == 0 ? 3.0 : 2.0) : 9.0; break;
                    case RhythmStyle::Free: w = 5.0; break;
                }
                weight[static_cast<std::size_t>(s)] = w;
            }
            int count = minCount + rng.below(maxCount - minCount + 1);
            count = static_cast<int>(std::lround(count * (static_cast<double>(slotsPerBar) / 8.0)));
            if (g.rhythm == RhythmStyle::Sustained) count = std::max(1, count / 2);
            count = std::clamp(count, 1, slotsPerBar);

            std::vector<int> chosen;
            if (g.rhythm != RhythmStyle::Syncopated || rng.unit() < 0.5) {
                chosen.push_back(0);
                weight[0] = 0.0;
            }
            while (static_cast<int>(chosen.size()) < count) {
                double total = 0.0;
                for (double w : weight) total += w;
                if (total <= 0.0) break;
                double pick = rng.unit() * total;
                int slot = 0;
                for (int s = 0; s < slotsPerBar; ++s) {
                    pick -= weight[static_cast<std::size_t>(s)];
                    if (pick < 0.0) { slot = s; break; }
                    slot = s;
                }
                if (weight[static_cast<std::size_t>(slot)] <= 0.0) break;
                chosen.push_back(slot);
                weight[static_cast<std::size_t>(slot)] = 0.0;
            }
            std::sort(chosen.begin(), chosen.end());
            pattern = chosen;
        }
        previousPattern = pattern;

        const bool lastBarOfPhrase = ((barStart - firstBarStart) / bar + 1) % phraseBars == 0 || barStart + bar >= range.end;
        for (std::size_t i = 0; i < pattern.size(); ++i) {
            const int slot = pattern[i];
            const Tick tick = barStart + static_cast<Tick>(slot) * slotTicks;
            if (tick < range.start || tick >= range.end) continue;
            Tick nextTick = i + 1 < pattern.size() ? barStart + static_cast<Tick>(pattern[i + 1]) * slotTicks : barStart + bar;
            Tick length = nextTick - tick;
            if (g.rhythm == RhythmStyle::Rhythmic) length = std::min(length, slotTicks * 2);
            length = std::min(length, range.end - tick);
            onsets.push_back({tick, std::max<Tick>(length, 1), strengthOf(slot), lastBarOfPhrase && i + 1 == pattern.size(), barNumber});
        }
    }

    // ---- pitch ------------------------------------------------------------------------------------------
    const double contourWeight = g.contour == Contour::Free ? 0.0 : 0.5;
    int prevPitch = -1;
    int prevPrevPitch = -1;
    Tick prevTick = -1;
    {
        int ctx = 0;
        if (previousKept(range.start, ctx)) prevPitch = ctx;
    }
    std::vector<int> barIntervals;      // intervals chosen in the current bar (by note index)
    std::vector<int> previousBarIntervals;
    int currentBar = -1;
    std::size_t noteInBar = 0;

    for (const auto& onset : onsets) {
        if (coveredAt(onset.tick)) continue;
        if (onset.barIndexInRange != currentBar) {
            previousBarIntervals = barIntervals;
            barIntervals.clear();
            currentBar = onset.barIndexInRange;
            noteInBar = 0;
        }
        {
            // a kept (locked or outside) note between the previous generated note and now is the better context
            Tick keptStart = -1;
            int ctx = 0;
            for (auto* n : kept) {
                if (n->startTick < onset.tick && n->startTick > keptStart) {
                    keptStart = n->startTick;
                    ctx = n->pitch;
                }
            }
            if (keptStart > prevTick) {
                prevPrevPitch = prevPitch;
                prevPitch = ctx;
                prevTick = keptStart;
            }
        }

        const auto* region = regionAt(regions, onset.tick);
        const std::uint16_t chordMask = region != nullptr ? region->chordMask : 0;
        const bool chordChanged = region != nullptr && prevTick >= 0 && regionAt(regions, prevTick) != region;
        const int rootPc = region != nullptr ? chordRootPitchClass(p.key, p.blocks[region->blockIndex].chord) : -1;

        const Tick sinceStart = onset.tick - range.start;
        const double u = static_cast<double>(sinceStart % phraseTicks) / static_cast<double>(phraseTicks);
        const double target = low + 2 + shape(g.contour, u) * static_cast<double>(std::max(0, high - low - 4));

        int bestPitch = low;
        double bestScore = -1e18;
        const int motifInterval = noteInBar < previousBarIntervals.size() ? previousBarIntervals[noteInBar] : 9999;

        // The jitter is drawn once per candidate in a fixed order, so results do not depend on early exits.
        for (int pitch = low; pitch <= high; ++pitch) {
            const int pc = mod12(pitch);
            const bool chordTone = ((chordMask >> pc) & 1u) != 0;
            const bool scaleTone = ((scaleMask >> pc) & 1u) != 0;
            double score = rng.unit() * 1.2;

            if (chordTone) score += onset.strength == 3 ? 9.0 : onset.strength == 2 ? 7.0 : onset.strength == 1 ? 5.0 : 3.0;
            else if (scaleTone) score += onset.strength >= 2 ? 0.5 : 2.5;
            else if (tension <= 0.0) score -= 1000.0;
            else score -= (1.0 - tension) * 8.0 + 2.0;

            if (!chordTone && onset.strength >= 1 && ((chordMask >> mod12(pc - 1)) & 1u) != 0) {
                score -= 3.0 * (1.0 - tension);  // half step above a chord tone clashes on a strong beat
            }

            score -= contourWeight * std::abs(static_cast<double>(pitch) - target);

            if (prevPitch >= 0) {
                const int d = std::abs(pitch - prevPitch);
                if (d == 0) {
                    score -= (prevPrevPitch == prevPitch) ? 4.0 : 1.5;
                    if (chordChanged && chordTone) score += 1.5;  // common tone held across the change
                } else if (d <= 2) score += 2.0;
                else if (d <= 4) score += 0.5;
                else if (d <= 7) score -= 1.0;
                else score -= static_cast<double>(d - 7) * 0.8 * (1.3 - tension * 0.9);
                if (tension < 0.7 && motifInterval != 9999) {
                    score -= 0.6 * std::abs((pitch - prevPitch) - motifInterval);
                }
            }

            if (onset.phraseEnd) {
                if (rootPc >= 0 && pc == rootPc) score += 8.0;
                else if (chordTone) score += 3.0;
            }

            if (score > bestScore) {
                bestScore = score;
                bestPitch = pitch;
            }
        }

        // Velocity: accents on strong beats, a softer phrase end, a little seeded variation.
        int velocity = 88 + (onset.strength == 3 ? 12 : onset.strength == 2 ? 6 : onset.strength == 0 ? -6 : 0) -
                       (onset.phraseEnd ? 8 : 0) + rng.below(9) - 4;
        velocity = std::clamp(velocity, 40, 120);

        MelodyNote note;
        note.pitch = bestPitch;
        note.startTick = onset.tick;
        note.durationTicks = std::min(onset.length, nextObstacleStart(onset.tick) - onset.tick);
        if (note.durationTicks < 1) continue;
        note.velocity = velocity;
        out.push_back(note);

        if (prevPitch >= 0) barIntervals.push_back(bestPitch - prevPitch);
        else barIntervals.push_back(0);
        ++noteInBar;
        prevPrevPitch = prevPitch;
        prevPitch = bestPitch;
        prevTick = onset.tick;
    }
    return out;
}

OpResult regenerateMelody(Progression& p, GenerationRange range) {
    const Tick limit = maxEndTick(p.timeSignature);
    range.start = std::max<Tick>(range.start, 0);
    range.end = std::min(range.end, limit);
    if (range.end <= range.start) return failure("nothing to generate: the range is empty");

    Progression work = p;
    work.melody.erase(std::remove_if(work.melody.begin(), work.melody.end(),
                                     [&](const MelodyNote& n) { return !n.locked && n.startTick >= range.start && n.startTick < range.end; }),
                      work.melody.end());
    const auto fresh = generateMelodyForRange(work, work.melodySettings.generator, range);
    if (work.melody.size() + fresh.size() > static_cast<std::size_t>(kMaxMelodyNotes)) return failure("the melody lane would be full");
    const std::size_t firstNew = work.melody.size();
    work.melody.insert(work.melody.end(), fresh.begin(), fresh.end());

    // sort and track the new notes
    std::vector<std::size_t> order(work.melody.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& x = work.melody[a];
        const auto& y = work.melody[b];
        return x.startTick != y.startTick ? x.startTick < y.startTick : x.pitch < y.pitch;
    });
    std::vector<MelodyNote> sorted;
    std::vector<std::size_t> newIndices;
    for (std::size_t i = 0; i < order.size(); ++i) {
        sorted.push_back(work.melody[order[i]]);
        if (order[i] >= firstNew) newIndices.push_back(i);
    }
    work.melody = std::move(sorted);

    // basis: forget old entries over the range, remember the chords now there
    auto& basis = work.melodyBasis;
    basis.erase(std::remove_if(basis.begin(), basis.end(),
                               [&](const MelodyBasisEntry& e) { return e.startTick < range.end && e.startTick + e.durationTicks > range.start; }),
                basis.end());
    for (const auto& b : work.blocks) {
        if (b.startTick < range.end && b.endTick() > range.start) basis.push_back({b.startTick, b.durationTicks, chordHash(b.chord)});
    }
    std::sort(basis.begin(), basis.end(), [](const MelodyBasisEntry& a, const MelodyBasisEntry& b) { return a.startTick < b.startTick; });

    p = std::move(work);
    OpResult r;
    r.ok = true;
    r.indices = newIndices;
    r.index = newIndices.empty() ? 0 : newIndices.front();
    return r;
}

std::vector<GenerationRange> affectedRanges(const Progression& p) {
    std::vector<GenerationRange> spans;
    if (p.melodyBasis.empty()) return spans;
    auto sameEntry = [](const MelodyBasisEntry& e, const ChordBlock& b) {
        return e.startTick == b.startTick && e.durationTicks == b.durationTicks && e.chordHash == chordHash(b.chord);
    };
    for (const auto& b : p.blocks) {
        bool found = false;
        for (const auto& e : p.melodyBasis) found = found || sameEntry(e, b);
        if (!found) spans.push_back({b.startTick, b.endTick()});
    }
    for (const auto& e : p.melodyBasis) {  // chords that no longer exist
        bool found = false;
        for (const auto& b : p.blocks) found = found || sameEntry(e, b);
        if (!found) spans.push_back({e.startTick, e.startTick + e.durationTicks});
    }
    std::sort(spans.begin(), spans.end(), [](const GenerationRange& a, const GenerationRange& b) { return a.start < b.start; });
    std::vector<GenerationRange> merged;
    for (const auto& s : spans) {
        if (!merged.empty() && s.start <= merged.back().end) merged.back().end = std::max(merged.back().end, s.end);
        else merged.push_back(s);
    }
    return merged;
}

OpResult regenerateAffectedMelody(Progression& p) {
    if (p.melodyBasis.empty()) return failure("no generated melody to compare against: use Generate first");
    const auto ranges = affectedRanges(p);
    if (ranges.empty()) return failure("no chords have changed since the melody was generated");
    Progression work = p;
    OpResult all;
    for (const auto& r : ranges) {
        const auto res = regenerateMelody(work, r);
        if (!res.ok) return res;
    }
    p = std::move(work);
    all.ok = true;
    return all;
}

}  // namespace chordcore
