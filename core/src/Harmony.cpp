#include <chordcore/Harmony.h>

#include <algorithm>

namespace chordcore {

namespace {

constexpr int kMajorScalePcs[7] = {0, 2, 4, 5, 7, 9, 11};

int mod12(int v) { return ((v % 12) + 12) % 12; }

// Position (1..7) of a pitch class (relative to the parent tonic) in the major scale, or 0 if it is not in it.
int majorDegreeOf(int relative) {
    for (int i = 0; i < 7; ++i)
        if (kMajorScalePcs[i] == relative) return i + 1;
    return 0;
}

Rgb average(const Rgb& a, const Rgb& b) {
    return Rgb{static_cast<std::uint8_t>((a.r + b.r + 1) / 2), static_cast<std::uint8_t>((a.g + b.g + 1) / 2),
               static_cast<std::uint8_t>((a.b + b.b + 1) / 2)};
}

constexpr const char* kMajorLabels[12] = {"1", "b2", "2", "b3", "3", "4", "#4", "5", "b6", "6", "b7", "7"};
constexpr const char* kMinorLabels[12] = {"1", "b2", "2", "3", "#3", "4", "#4", "5", "6", "#6", "7", "#7"};

}  // namespace

std::array<Rgb, 7> paletteColours(PaletteId palette) {
    switch (palette) {
        case PaletteId::Soft:
            return {Rgb{0xe8, 0x9a, 0x96}, Rgb{0xeb, 0xc2, 0x8c}, Rgb{0xdc, 0xd0, 0x8c}, Rgb{0x9c, 0xcb, 0x9c},
                    Rgb{0x8f, 0xc3, 0xd6}, Rgb{0xb0, 0xa2, 0xdc}, Rgb{0xd9, 0xa4, 0xcc}};
        case PaletteId::ColourBlindSafe:  // Okabe-Ito based, ordered by degree
            return {Rgb{0xd5, 0x5e, 0x00}, Rgb{0xe6, 0x9f, 0x00}, Rgb{0xf0, 0xe4, 0x42}, Rgb{0x00, 0x9e, 0x73},
                    Rgb{0x56, 0xb4, 0xe9}, Rgb{0x00, 0x72, 0xb2}, Rgb{0xcc, 0x79, 0xa7}};
        case PaletteId::Classic:
            break;
    }
    return {Rgb{0xd6, 0x60, 0x5a}, Rgb{0xdb, 0x9b, 0x48}, Rgb{0xb8, 0xa2, 0x48}, Rgb{0x68, 0xa8, 0x68},
            Rgb{0x4f, 0xa0, 0xbe}, Rgb{0x7e, 0x6a, 0xc4}, Rgb{0xb8, 0x74, 0xac}};
}

Rgb colourForPitchClass(int parentMajorTonic, int pitchClass, PaletteId paletteId) {
    const auto palette = paletteColours(paletteId);
    const int relative = mod12(pitchClass - parentMajorTonic);
    if (const int degree = majorDegreeOf(relative)) return palette[static_cast<std::size_t>(degree - 1)];
    // outside the major scale: midpoint of the two degrees it lies between
    int lower = 0;
    for (int i = 0; i < 7; ++i)
        if (kMajorScalePcs[i] < relative) lower = i;
    return average(palette[static_cast<std::size_t>(lower)], palette[static_cast<std::size_t>(lower + 1)]);
}

std::uint16_t scalePitchClassMask(const Key& key) {
    std::uint16_t mask = 0;
    for (int degree = 1; degree <= 7; ++degree) mask = static_cast<std::uint16_t>(mask | (1u << mod12(key.tonic + scaleDegreeSemitones(key.mode, degree))));
    return mask;
}

std::uint16_t chordPitchClassMask(const Key& key, const ChordSpec& chord) {
    std::uint16_t mask = 0;
    const int root = chordRootPitchClass(key, chord);
    for (int interval : chordIntervals(chord)) mask = static_cast<std::uint16_t>(mask | (1u << mod12(root + interval)));
    return mask;
}

HarmonyResolver::HarmonyResolver(const Key& key, const HarmonicSettings& settings) : key_(key), settings_(settings) {
    scaleMask_ = scalePitchClassMask(key);
    const int parentTonic = (settings.mapping == ColourMapping::DiatonicCentric && key.mode == Mode::Minor) ? mod12(key.tonic + 3) : key.tonic;
    const bool ownScaleLabels = settings.mapping == ColourMapping::DiatonicCentric && key.mode == Mode::Minor;
    for (int pc = 0; pc < 12; ++pc) {
        PitchIdentity& id = table_[static_cast<std::size_t>(pc)];
        id.pitchClass = pc;
        id.chromaticDegree = mod12(pc - key.tonic);
        id.inScale = ((scaleMask_ >> pc) & 1u) != 0;
        id.colour = colourForPitchClass(parentTonic, pc, settings.palette);
        id.label = (ownScaleLabels ? kMinorLabels : kMajorLabels)[id.chromaticDegree];
        const bool flat = id.label.front() == 'b';
        const bool sharp = id.label.front() == '#';
        id.chromaticAlteration = flat ? -1 : sharp ? 1 : 0;
        id.diatonicDegree = id.label.back() - '0';
    }
}

const PitchIdentity& HarmonyResolver::identity(int pitch) const noexcept { return table_[static_cast<std::size_t>(mod12(pitch))]; }

Compat HarmonyResolver::classify(int pitch, std::uint16_t chordMask) const noexcept {
    const int pc = mod12(pitch);
    if ((chordMask >> pc) & 1u) return Compat::ChordTone;
    return identity(pc).inScale ? Compat::ScaleTone : Compat::Chromatic;
}

Rgba HarmonyResolver::backgroundFor(Compat compat, const Rgb& c) noexcept {
    switch (compat) {
        case Compat::ChordTone: return Rgba{c.r, c.g, c.b, 120};
        case Compat::ScaleTone: return Rgba{c.r, c.g, c.b, 34};
        case Compat::Chromatic: break;
    }
    return Rgba{24, 24, 28, 255};
}

HarmonicVisualInfo HarmonyResolver::visual(int pitch, std::uint16_t chordMask) const {
    const PitchIdentity& id = identity(pitch);
    HarmonicVisualInfo v;
    v.chromaticDegree = id.chromaticDegree;
    v.diatonicDegree = id.diatonicDegree;
    v.chromaticAlteration = id.chromaticAlteration;
    v.isChordTone = ((chordMask >> id.pitchClass) & 1u) != 0;
    v.isScaleTone = id.inScale;
    v.isChromatic = !id.inScale;
    v.compatibility = classify(pitch, chordMask);
    v.noteColour = id.colour;
    v.backgroundColour = backgroundFor(v.compatibility, id.colour);
    v.label = id.label;
    return v;
}

std::vector<CompatibilityRegion> buildCompatibility(const Progression& p) {
    std::vector<CompatibilityRegion> regions;
    regions.reserve(p.blocks.size());
    for (std::size_t i = 0; i < p.blocks.size(); ++i) {
        const ChordBlock& b = p.blocks[i];
        regions.push_back({b.startTick, b.endTick(), chordPitchClassMask(p.key, b.chord), i});
    }
    std::stable_sort(regions.begin(), regions.end(), [](const CompatibilityRegion& a, const CompatibilityRegion& b) { return a.startTick < b.startTick; });
    return regions;
}

const CompatibilityRegion* regionAt(const std::vector<CompatibilityRegion>& regions, Tick tick) noexcept {
    // regions are sorted and do not overlap: find the last region starting at or before the tick
    auto it = std::upper_bound(regions.begin(), regions.end(), tick, [](Tick t, const CompatibilityRegion& r) { return t < r.startTick; });
    if (it == regions.begin()) return nullptr;
    --it;
    return tick < it->endTick ? &*it : nullptr;
}

}  // namespace chordcore
