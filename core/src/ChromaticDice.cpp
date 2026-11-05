#include <chordcore/ChromaticDice.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>

#include <chordcore/ChordEdit.h>
#include <chordcore/RomanNumeral.h>

namespace chordcore {

namespace {

struct Rng {  // SplitMix64, identical on every platform
    std::uint64_t state;
    explicit Rng(std::uint64_t seed) : state(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
};

int floorMod12(int v) { return ((v % 12) + 12) % 12; }

// Root as (degree, accidental) of the project key: the nearest scale degree, flats preferred, the tritone is #4.
bool degreeFor(const Key& key, int rootRel, ChordSpec& out) {
    int bestDegree = 0, bestAcc = 0, bestCost = 99;
    for (int degree = 1; degree <= 7; ++degree) {
        const int diff = rootRel - scaleDegreeSemitones(key.mode, degree);
        int d = diff;
        if (d > 6) d -= 12;
        if (d < -6) d += 12;
        if (d < -1 || d > 1) continue;
        int cost = std::abs(d) * 10 + (d > 0 ? 1 : 0);  // exact first, then flat before sharp
        if (rootRel == 6 && d == 1) cost = 5;            // the tritone reads as a sharpened fourth (#iv)
        if (cost < bestCost) { bestCost = cost; bestDegree = degree; bestAcc = d; }
    }
    if (bestDegree == 0) return false;
    out.degree = bestDegree;
    out.accidental = bestAcc;
    return true;
}

struct Shape {  // chord described by its intervals above the root
    int third, fifth;
    int seventh;  // -1 none
    int ninth;    // -1 none (13, 14 or 15)
};

bool specFor(const Key& key, int rootRel, const Shape& s, ChordSpec& out) {
    ChordSpec c;
    if (!degreeFor(key, rootRel, c)) return false;
    if (s.third == 4 && s.fifth == 7) c.quality = Quality::Major;
    else if (s.third == 3 && s.fifth == 7) c.quality = Quality::Minor;
    else if (s.third == 3 && s.fifth == 6) c.quality = Quality::Diminished;
    else if (s.third == 4 && s.fifth == 8) c.quality = Quality::Augmented;
    else if (s.third == 2 || s.third == 5) {
        c.quality = Quality::Major;
        c.suspension = s.third == 2 ? Suspension::Sus2 : Suspension::Sus4;
        if (s.fifth == 6) c.quality = Quality::Diminished;
        else if (s.fifth == 8) c.quality = Quality::Augmented;
        else if (s.fifth != 7) return false;
    } else return false;
    if (s.seventh == 10) c.seventh = Seventh::Minor;
    else if (s.seventh == 11) c.seventh = Seventh::Major;
    else if (s.seventh == 9) c.seventh = Seventh::Diminished;
    else if (s.seventh != -1) return false;
    if (s.ninth == 13) c.ninth = Ninth::Minor;
    else if (s.ninth == 14) c.ninth = Ninth::Major;
    else if (s.ninth == 15) c.ninth = Ninth::Sharp;
    else if (s.ninth != -1) return false;
    if (!c.isValid()) return false;
    // The chord model must reproduce exactly the intervals that were asked for.
    std::vector<int> want{0, s.third, s.fifth};
    if (s.seventh != -1) want.push_back(s.seventh);
    if (s.ninth != -1) want.push_back(s.ninth);
    if (chordIntervals(c) != want) return false;
    if (chordRootPitchClass(key, c) != floorMod12(key.tonic + rootRel)) return false;
    out = c;
    return true;
}

using ContentKey = std::pair<int, std::vector<int>>;  // root pitch class, intervals

ContentKey contentOf(const Key& key, const ChordSpec& c) { return {chordRootPitchClass(key, c), chordIntervals(c)}; }

std::set<int> pitchClassesOf(const ContentKey& k) {
    std::set<int> out;
    for (int i : k.second) out.insert(floorMod12(k.first + i));
    return out;
}

int fifthsDistance(int a, int b) {
    const int pa = (a * 7) % 12, pb = (b * 7) % 12;
    const int d = std::abs(pa - pb);
    return std::min(d, 12 - d);
}

}  // namespace

std::vector<int> parallelModeScale(ParallelMode mode) {
    switch (mode) {
        case ParallelMode::Ionian: return {0, 2, 4, 5, 7, 9, 11};
        case ParallelMode::Dorian: return {0, 2, 3, 5, 7, 9, 10};
        case ParallelMode::Phrygian: return {0, 1, 3, 5, 7, 8, 10};
        case ParallelMode::Lydian: return {0, 2, 4, 6, 7, 9, 11};
        case ParallelMode::Mixolydian: return {0, 2, 4, 5, 7, 9, 10};
        case ParallelMode::Aeolian: return {0, 2, 3, 5, 7, 8, 10};
        case ParallelMode::Locrian: return {0, 1, 3, 5, 6, 8, 10};
        case ParallelMode::HarmonicMinor: return {0, 2, 3, 5, 7, 8, 11};
        case ParallelMode::MelodicMinor: return {0, 2, 3, 5, 7, 9, 11};
    }
    return {};
}

const char* toString(ParallelMode m) {
    switch (m) {
        case ParallelMode::Ionian: return "Ionian";
        case ParallelMode::Dorian: return "Dorian";
        case ParallelMode::Phrygian: return "Phrygian";
        case ParallelMode::Lydian: return "Lydian";
        case ParallelMode::Mixolydian: return "Mixolydian";
        case ParallelMode::Aeolian: return "Aeolian";
        case ParallelMode::Locrian: return "Locrian";
        case ParallelMode::HarmonicMinor: return "Harmonic minor";
        case ParallelMode::MelodicMinor: return "Melodic minor";
    }
    return "";
}

const char* toString(ModeFilter f) {
    if (f == ModeFilter::Any) return "Any";
    return toString(static_cast<ParallelMode>(static_cast<int>(f) - 1));
}
const char* toString(DiceMode m) { return m == DiceMode::PreserveRoot ? "Preserve Root" : "Replace Harmony"; }
const char* toString(DiceComplexity c) {
    switch (c) {
        case DiceComplexity::Conservative: return "Conservative";
        case DiceComplexity::Adventurous: return "Adventurous";
        case DiceComplexity::Unrestricted: return "Unrestricted";
    }
    return "";
}

std::uint64_t diceRollSeed(const DiceSettings& s, Tick blockStart) {
    return (static_cast<std::uint64_t>(s.seed) << 32) ^ (static_cast<std::uint64_t>(s.rollIndex) * 0x9E3779B97F4A7C15ull) ^
           (static_cast<std::uint64_t>(blockStart) * 0xD6E8FEB86659FD93ull);
}

std::vector<DiceCandidate> diceCandidates(const Progression& p, std::size_t blockIndex, const DiceSettings& settings,
                                          const std::vector<ChordSpec>* recent) {
    if (blockIndex >= p.blocks.size()) throw std::out_of_range("block index");
    const Key& key = p.key;
    const ChordBlock& block = p.blocks[blockIndex];
    const ContentKey current = contentOf(key, block.chord);
    const std::set<int> currentPcs = pitchClassesOf(current);

    // Neighbours in time order, for repeat avoidance and adjacent-chord scoring.
    std::vector<std::size_t> order(p.blocks.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return p.blocks[a].startTick < p.blocks[b].startTick; });
    const std::size_t pos = static_cast<std::size_t>(std::find(order.begin(), order.end(), blockIndex) - order.begin());
    std::vector<ContentKey> neighbours;
    if (pos > 0) neighbours.push_back(contentOf(key, p.blocks[order[pos - 1]].chord));
    if (pos + 1 < order.size()) neighbours.push_back(contentOf(key, p.blocks[order[pos + 1]].chord));

    // --- Generate: stack thirds on every degree of every allowed parallel mode. ---------------------------------
    const bool filtered = settings.sourceFilter != ModeFilter::Any;
    std::vector<ParallelMode> modes;
    for (int m = 0; m < kParallelModeCount; ++m) {
        const auto mode = static_cast<ParallelMode>(m);
        const bool conservativeMode = mode == ParallelMode::Ionian || mode == ParallelMode::Dorian || mode == ParallelMode::Mixolydian ||
                                      mode == ParallelMode::Aeolian;
        if (filtered || settings.complexity != DiceComplexity::Conservative || conservativeMode) modes.push_back(mode);
    }
    struct Raw { ChordSpec chord; ContentKey content; std::vector<ParallelMode> sources; };
    std::vector<Raw> raws;
    std::map<ContentKey, std::size_t> index;
    auto addRaw = [&](const ChordSpec& chord, const std::vector<ParallelMode>& src) {
        const ContentKey k = contentOf(key, chord);
        auto it = index.find(k);
        if (it == index.end()) {
            index[k] = raws.size();
            raws.push_back({chord, k, src});
        } else {
            auto& existing = raws[it->second].sources;
            for (ParallelMode m : src) {
                if (std::find(existing.begin(), existing.end(), m) == existing.end()) existing.push_back(m);
            }
        }
    };
    for (ParallelMode mode : modes) {
        const std::vector<int> scale = parallelModeScale(mode);
        for (int d = 0; d < 7; ++d) {
            auto at = [&](int step) { return scale[static_cast<std::size_t>((d + step) % 7)]; };
            auto above = [&](int step) { return floorMod12(at(step) - at(0)); };
            Shape triad{above(2), above(4), -1, -1};
            Shape seventh{above(2), above(4), above(6), -1};
            const int ninthSemis = floorMod12(at(8) - at(0));  // 1, 2 or 3 above the octave
            Shape ninth{above(2), above(4), above(6), 12 + ninthSemis};
            ChordSpec spec;
            const int rootRel = at(0);
            if (specFor(key, rootRel, triad, spec)) addRaw(spec, {mode});
            if (specFor(key, rootRel, seventh, spec)) addRaw(spec, {mode});
            if (settings.complexity != DiceComplexity::Conservative && specFor(key, rootRel, ninth, spec)) addRaw(spec, {mode});
        }
    }
    if (settings.complexity == DiceComplexity::Unrestricted && !filtered) {  // any root, any quality: chromatic
        static const Shape kCatalogue[] = {{4, 7, -1, -1}, {3, 7, -1, -1}, {3, 6, -1, -1}, {4, 8, -1, -1}, {4, 7, 10, -1}, {4, 7, 11, -1},
                                           {3, 7, 10, -1}, {3, 6, 10, -1}, {3, 6, 9, -1},  {3, 7, 11, -1}, {2, 7, -1, -1}, {5, 7, -1, -1},
                                           {5, 7, 10, -1}, {4, 7, 10, 14}, {4, 7, 11, 14}, {3, 7, 10, 14}, {4, 7, 10, 13}, {4, 7, 10, 15}};
        for (int rootRel = 0; rootRel < 12; ++rootRel) {
            for (const Shape& s : kCatalogue) {
                ChordSpec spec;
                if (specFor(key, rootRel, s, spec)) addRaw(spec, {});
            }
        }
    }

    // --- Filter. -------------------------------------------------------------------------------------------------
    std::vector<ContentKey> avoid;
    if (settings.avoidRepeats) {
        avoid = neighbours;
        if (recent != nullptr) for (const auto& c : *recent) avoid.push_back(contentOf(key, c));
    }
    std::vector<DiceCandidate> pool;
    for (const Raw& raw : raws) {
        if (raw.content == current) continue;  // must change something
        if (settings.keepsRoot() && raw.content.first != current.first) continue;
        if (filtered) {
            const auto wanted = static_cast<ParallelMode>(static_cast<int>(settings.sourceFilter) - 1);
            if (std::find(raw.sources.begin(), raw.sources.end(), wanted) == raw.sources.end()) continue;
        }
        if (std::find(avoid.begin(), avoid.end(), raw.content) != avoid.end()) continue;

        DiceCandidate c;
        c.chord = raw.chord;
        c.chord.inversion = std::min(block.chord.inversion, maxInversion(raw.chord));
        c.rootPitchClass = raw.content.first;
        c.sources = raw.sources;
        std::sort(c.sources.begin(), c.sources.end());

        VoicingOptions options;  // the new chord must still be playable at this block's octave
        options.rootBase += 12 * (p.octave + block.octave);
        try { (void)voiceChord(key, c.chord, options); } catch (const std::out_of_range&) { continue; }

        // Score: shared tones, root motion, small voice movement, fit with the neighbours, less tension.
        const std::set<int> pcs = pitchClassesOf(raw.content);
        int shared = 0, movement = 0;
        for (int pc : pcs) {
            if (currentPcs.count(pc) != 0) ++shared;
            int best = 6;
            for (int q : currentPcs) { const int d = std::abs(pc - q); best = std::min(best, std::min(d, 12 - d)); }
            movement += best;
        }
        int score = 4 * shared + 2 * (6 - fifthsDistance(c.rootPitchClass, current.first)) - movement;
        for (const auto& n : neighbours) score += 6 - fifthsDistance(c.rootPitchClass, n.first);
        int tension = 0;
        for (int pc : pcs) {
            bool diatonic = false;
            for (int degree = 1; degree <= 7; ++degree) diatonic = diatonic || floorMod12(key.tonic + scaleDegreeSemitones(key.mode, degree)) == pc;
            if (!diatonic) ++tension;
        }
        for (int interval : raw.content.second) if (interval > 12) ++tension;
        const int tensionWeight = settings.complexity == DiceComplexity::Conservative ? 2 : settings.complexity == DiceComplexity::Adventurous ? 1 : 0;
        score -= tension * tensionWeight;
        c.score = score;
        if (auto roman = formatRomanNumeral(c.chord)) c.roman = *roman;
        c.name = chordName(key, c.chord);
        pool.push_back(std::move(c));
    }
    std::stable_sort(pool.begin(), pool.end(), [](const DiceCandidate& a, const DiceCandidate& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.rootPitchClass != b.rootPitchClass) return a.rootPitchClass < b.rootPitchClass;
        return chordIntervals(a.chord) < chordIntervals(b.chord);
    });
    return pool;
}

DiceResult rollChromaticDice(const Progression& p, std::size_t blockIndex, const DiceSettings& settings,
                             const std::vector<ChordSpec>* recent) {
    DiceResult result;
    result.chord = p.blocks.at(blockIndex).chord;
    const auto pool = diceCandidates(p, blockIndex, settings, recent);
    result.poolSize = static_cast<int>(pool.size());
    if (pool.empty()) {
        result.message = "No eligible chord for these settings - the chord was left unchanged. Try Replace Harmony, Adventurous, or Source Mode: Any.";
        return result;
    }
    // Integer weights from the score; Conservative squares/cubes the gap to favour the best, Unrestricted is nearly flat.
    const int minScore = pool.back().score;
    const int power = settings.complexity == DiceComplexity::Conservative ? 3 : settings.complexity == DiceComplexity::Adventurous ? 2 : 1;
    std::vector<std::uint64_t> weights;
    std::uint64_t total = 0;
    for (const auto& c : pool) {
        std::uint64_t w = 1;
        for (int i = 0; i < power; ++i) w *= static_cast<std::uint64_t>(c.score - minScore + 1);
        weights.push_back(w);
        total += w;
    }
    Rng rng(diceRollSeed(settings, p.blocks[blockIndex].startTick));
    std::uint64_t pick = rng.next() % total;
    std::size_t chosen = 0;
    while (chosen + 1 < weights.size() && pick >= weights[chosen]) { pick -= weights[chosen]; ++chosen; }

    result.ok = true;
    result.picked = pool[chosen];
    result.chord = pool[chosen].chord;
    const int wantedInversion = p.blocks[blockIndex].chord.inversion;
    if (result.chord.inversion != wantedInversion) {
        result.normalized = true;
        result.normalizationNote = "Inversion " + std::to_string(wantedInversion) + " is not available on " + result.picked.name +
                                   "; it was reduced to " + std::to_string(result.chord.inversion) + ".";
    }
    std::string sources;
    for (ParallelMode m : result.picked.sources) sources += (sources.empty() ? "" : ", ") + std::string(toString(m));
    result.message = result.picked.name + (result.picked.roman.empty() ? "" : "  (" + result.picked.roman + ")") +
                     (sources.empty() ? "  chromatic" : "  from " + sources);
    if (result.normalized) result.message += ". " + result.normalizationNote;
    return result;
}

}  // namespace chordcore
