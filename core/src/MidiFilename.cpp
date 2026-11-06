#include <chordcore/MidiFilename.h>

#include <algorithm>
#include <cstdint>

#include <chordcore/ChordEdit.h>
#include <chordcore/RomanNumeral.h>

namespace chordcore {

namespace {

const char* const kFlat = "\xE2\x99\xAD";   // U+266D
const char* const kSharp = "\xE2\x99\xAF";  // U+266F
const char* const kDegree = "\xC2\xB0";     // U+00B0
const char* const kEnDash = "\xE2\x80\x93";

// Chord names and numerals only use a lowercase 'b' for flats and '#' for sharps (note letters are uppercase, the
// quality words dim/aug/sus/add/maj contain no 'b'), so a character-wise map is exact.
std::string toDisplay(const std::string& text, bool roman) {
    std::string out;
    for (char c : text) {
        if (c == 'b') out += kFlat;
        else if (c == '#') out += kSharp;
        else if (roman && c == 'o') out += kDegree;
        else out += c;
    }
    return out;
}

// ASCII form: the half-diminished sign has no ASCII twin, "m7b5" is the conventional spelling.
std::string toAscii(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (text.compare(i, 2, "\xC3\xB8") == 0) { out += "m7b5"; i += 2; continue; }
        if (static_cast<unsigned char>(text[i]) >= 0x80) { out += '?'; ++i; continue; }
        out += text[i++];
    }
    return out;
}

std::string dash(bool ascii) { return std::string(" ") + (ascii ? "-" : kEnDash) + " "; }

std::size_t unitsOf(const std::string& utf8) {  // UTF-16 code units
    std::size_t n = 0;
    for (unsigned char c : utf8) {
        if ((c & 0xC0) == 0x80) continue;  // continuation byte
        n += (c >= 0xF0) ? 2 : 1;
    }
    return n;
}

std::string truncateToUnits(const std::string& utf8, std::size_t maxUnits) {
    std::string out;
    std::size_t units = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        const std::size_t len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
        const std::size_t u = len == 4 ? 2 : 1;
        if (units + u > maxUnits) break;
        out.append(utf8, i, len);
        units += u;
        i += len;
    }
    return out;
}

std::string hash6(const std::string& text) {  // FNV-1a, 24 bits shown
    std::uint32_t h = 2166136261u;
    for (unsigned char c : text) { h ^= c; h *= 16777619u; }
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (int shift = 20; shift >= 0; shift -= 4) out += digits[(h >> shift) & 0xF];
    return out;
}

std::string sanitize(std::string s) {
    for (char& c : s) {
        if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || static_cast<unsigned char>(c) < 0x20) c = '_';
    }
    return s;
}

}  // namespace

const char* toString(MidiFilenameFormat f) {
    switch (f) {
        case MidiFilenameFormat::Absolute: return "Absolute";
        case MidiFilenameFormat::Roman: return "Roman";
        case MidiFilenameFormat::Both: return "Both";
    }
    return "Absolute";
}

std::string chordLabel(const Key& key, const ChordSpec& chord, MidiFilenameFormat format, bool ascii) {
    const std::string absolute = chordName(key, chord);
    const auto roman = chromaticRomanNumeral(key, chord);
    auto render = [&](const std::string& s, bool isRoman) { return ascii ? toAscii(s) : toDisplay(s, isRoman); };
    switch (format) {
        case MidiFilenameFormat::Absolute: return render(absolute, false);
        case MidiFilenameFormat::Roman: return roman ? render(*roman, true) : render(absolute, false);
        case MidiFilenameFormat::Both: return roman ? render(absolute, false) + " (" + render(*roman, true) + ")" : render(absolute, false);
    }
    return render(absolute, false);
}

std::vector<std::size_t> chordsInRegion(const Progression& p, int firstBar, int lastBar) {
    std::vector<std::size_t> order;
    const bool ranged = firstBar != 0 || lastBar != 0;
    const Tick bar = p.timeSignature.ticksPerBar();
    const Tick from = static_cast<Tick>(std::max(firstBar, 1) - 1) * bar;
    const Tick to = static_cast<Tick>(lastBar) * bar;
    for (std::size_t i = 0; i < p.blocks.size(); ++i) {
        if (ranged && (p.blocks[i].endTick() <= from || p.blocks[i].startTick >= to)) continue;
        order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return p.blocks[a].startTick < p.blocks[b].startTick; });
    return order;
}

std::string progressionLabel(const Progression& p, MidiFilenameFormat format, int firstBar, int lastBar, bool ascii) {
    std::string out;
    for (std::size_t i : chordsInRegion(p, firstBar, lastBar)) {
        if (!out.empty()) out += dash(ascii);
        out += chordLabel(p.key, p.blocks[i].chord, format, ascii);
    }
    return out;
}

std::string generateMidiFilename(const Progression& p, MidiFilenameFormat format, const ProjectExportOptions& options, bool ascii) {
    if (options.part == ExportPart::Melody) return exportFileName(ExportPart::Melody);  // the existing melody convention

    std::string suffix;
    if (!options.originalVoicing) suffix += "_voiced";
    if (options.part == ExportPart::Combined) suffix += "_combined";
    const std::string tail = suffix + ".mid";

    std::vector<std::string> tokens;
    for (std::size_t i : chordsInRegion(p, options.firstBar, options.lastBar)) tokens.push_back(sanitize(chordLabel(p.key, p.blocks[i].chord, format, ascii)));
    if (tokens.empty()) tokens.push_back("No chords");

    const std::string separator = dash(ascii);
    auto join = [&](std::size_t count) {
        std::string s;
        for (std::size_t i = 0; i < count; ++i) s += (i ? separator : "") + tokens[i];
        return s;
    };
    const std::string full = join(tokens.size());
    if (unitsOf(full) + unitsOf(tail) <= static_cast<std::size_t>(kMaxFilenameUnits)) return full + tail;

    // Too long: whole chords from the start, then "+N more [hash]". The hash is of the complete name, so two
    // different long progressions that share a start never collide.
    const std::string id = hash6(full + tail);
    for (std::size_t keep = tokens.size() - 1; keep >= 1; --keep) {
        const std::string ending = separator + "+" + std::to_string(tokens.size() - keep) + " more [" + id + "]";
        const std::string stem = join(keep) + ending;
        if (unitsOf(stem) + unitsOf(tail) <= static_cast<std::size_t>(kMaxFilenameUnits)) return stem + tail;
    }
    // Even the first chord does not fit: cut it between characters.
    const std::string ending = separator + "+" + std::to_string(tokens.size() - 1) + " more [" + id + "]";
    const std::size_t room = static_cast<std::size_t>(kMaxFilenameUnits) - unitsOf(tail) - unitsOf(ending);
    return truncateToUnits(tokens.front(), room) + ending + tail;
}

std::vector<std::string> midiMetadataText(const Progression& p, int firstBar, int lastBar) {
    std::vector<std::string> out;
    if (chordsInRegion(p, firstBar, lastBar).empty()) return out;
    out.push_back("Absolute Progression: " + progressionLabel(p, MidiFilenameFormat::Absolute, firstBar, lastBar, true));
    out.push_back("Roman Progression: " + progressionLabel(p, MidiFilenameFormat::Roman, firstBar, lastBar, true));
    out.push_back("Project Key: " + noteName(p.key, p.key.tonic) + (p.key.mode == Mode::Major ? " major" : " minor"));
    return out;
}

}  // namespace chordcore
