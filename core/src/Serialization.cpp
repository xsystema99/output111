#include <chordcore/Serialization.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace chordcore {

namespace {

// ------------------------------------------------------------------------------------------------
// Minimal JSON reader (objects, arrays, strings, numbers, true/false/null). Strict about syntax.
// ------------------------------------------------------------------------------------------------

struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> array;
    std::vector<std::string> keys;  // object members, in file order (parallel to `values`)
    std::vector<Json> values;

    const Json* find(const std::string& key) const {
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (keys[i] == key) return &values[i];
        }
        return nullptr;
    }
};

constexpr int kMaxDepth = 64;

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : s_(text) {}

    bool parse(Json& out, std::string& error) {
        skipWhitespace();
        if (!value(out, 0)) {
            error = error_;
            return false;
        }
        skipWhitespace();
        if (pos_ != s_.size()) {
            error = "unexpected data after the end of the JSON value at offset " + std::to_string(pos_);
            return false;
        }
        return true;
    }

private:
    std::string_view s_;
    std::size_t pos_ = 0;
    std::string error_;

    bool fail(const std::string& message) {
        if (error_.empty()) error_ = message + " at offset " + std::to_string(pos_);
        return false;
    }

    void skipWhitespace() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    bool literal(std::string_view word) {
        if (s_.substr(pos_, word.size()) != word) return fail("invalid literal");
        pos_ += word.size();
        return true;
    }

    bool value(Json& out, int depth) {
        if (depth > kMaxDepth) return fail("nesting too deep");
        if (pos_ >= s_.size()) return fail("unexpected end of input");
        const char c = s_[pos_];
        if (c == '{') return object(out, depth);
        if (c == '[') return array(out, depth);
        if (c == '"') {
            out.type = Json::Type::String;
            return string(out.string);
        }
        if (c == 't') { out.type = Json::Type::Bool; out.boolean = true; return literal("true"); }
        if (c == 'f') { out.type = Json::Type::Bool; out.boolean = false; return literal("false"); }
        if (c == 'n') { out.type = Json::Type::Null; return literal("null"); }
        if (c == '-' || (c >= '0' && c <= '9')) return number(out);
        return fail("unexpected character");
    }

    bool number(Json& out) {
        const std::size_t start = pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E') ++pos_;
            else break;
        }
        const char* first = s_.data() + start;
        const char* last = s_.data() + pos_;
        double v = 0.0;
        const auto result = std::from_chars(first, last, v);
        if (result.ec != std::errc() || result.ptr != last) {
            pos_ = start;
            return fail("invalid number");
        }
        if (!std::isfinite(v)) {
            pos_ = start;
            return fail("number out of range");
        }
        out.type = Json::Type::Number;
        out.number = v;
        return true;
    }

    static void appendUtf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }

    bool string(std::string& out) {
        ++pos_;  // opening quote
        out.clear();
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) { --pos_; return fail("control character in string"); }
            if (c != '\\') { out.push_back(c); continue; }
            if (pos_ >= s_.size()) break;
            const char e = s_[pos_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) return fail("truncated \\u escape");
                    unsigned code = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = s_[pos_++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                        else return fail("invalid \\u escape");
                    }
                    appendUtf8(out, code);
                    break;
                }
                default: --pos_; return fail("invalid escape sequence");
            }
        }
        return fail("unterminated string");
    }

    bool array(Json& out, int depth) {
        out.type = Json::Type::Array;
        ++pos_;
        skipWhitespace();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            skipWhitespace();
            out.array.emplace_back();
            if (!value(out.array.back(), depth + 1)) return false;
            skipWhitespace();
            if (pos_ >= s_.size()) return fail("unterminated array");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return fail("expected ',' or ']'");
        }
    }

    bool object(Json& out, int depth) {
        out.type = Json::Type::Object;
        ++pos_;
        skipWhitespace();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            skipWhitespace();
            if (pos_ >= s_.size() || s_[pos_] != '"') return fail("expected a string key");
            std::string key;
            if (!string(key)) return false;
            for (const auto& existing : out.keys) {
                if (existing == key) return fail("duplicate key '" + key + "'");
            }
            skipWhitespace();
            if (pos_ >= s_.size() || s_[pos_] != ':') return fail("expected ':'");
            ++pos_;
            skipWhitespace();
            out.keys.push_back(std::move(key));
            out.values.emplace_back();
            if (!value(out.values.back(), depth + 1)) return false;
            skipWhitespace();
            if (pos_ >= s_.size()) return fail("unterminated object");
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return fail("expected ',' or '}'");
        }
    }
};

// ------------------------------------------------------------------------------------------------
// Enum <-> string tables
// ------------------------------------------------------------------------------------------------

const char* toString(Mode m) { return m == Mode::Major ? "major" : "minor"; }

const char* toString(Quality q) {
    switch (q) {
        case Quality::Major: return "major";
        case Quality::Minor: return "minor";
        case Quality::Diminished: return "diminished";
        case Quality::Augmented: return "augmented";
    }
    return "major";
}

const char* toString(Seventh s) {
    switch (s) {
        case Seventh::None: return "none";
        case Seventh::Minor: return "minor";
        case Seventh::Major: return "major";
        case Seventh::Diminished: return "diminished";
    }
    return "none";
}

const char* toString(Ninth n) {
    switch (n) {
        case Ninth::None: return "none";
        case Ninth::Minor: return "minor";
        case Ninth::Major: return "major";
        case Ninth::Sharp: return "sharp";
    }
    return "none";
}

const char* toString(Eleventh e) {
    switch (e) {
        case Eleventh::None: return "none";
        case Eleventh::Perfect: return "perfect";
        case Eleventh::Sharp: return "sharp";
    }
    return "none";
}

const char* toString(Thirteenth t) {
    switch (t) {
        case Thirteenth::None: return "none";
        case Thirteenth::Minor: return "minor";
        case Thirteenth::Major: return "major";
    }
    return "none";
}

const char* toString(ColourMapping m) { return m == ColourMapping::MajorCentric ? "major" : "diatonic"; }

const char* toString(PaletteId p) {
    switch (p) {
        case PaletteId::Classic: return "classic";
        case PaletteId::Soft: return "soft";
        case PaletteId::ColourBlindSafe: return "colourBlindSafe";
    }
    return "classic";
}

const char* toString(LabelMode l) {
    switch (l) {
        case LabelMode::None: return "none";
        case LabelMode::Pitch: return "pitch";
        case LabelMode::Degree: return "degree";
        case LabelMode::Both: return "both";
    }
    return "degree";
}

const char* toString(Suspension s) {
    switch (s) {
        case Suspension::None: return "none";
        case Suspension::Sus2: return "sus2";
        case Suspension::Sus4: return "sus4";
    }
    return "none";
}

std::string number(double v) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, v);  // shortest exact round trip
    return std::string(buffer, result.ptr);
}

// ------------------------------------------------------------------------------------------------
// Typed access with path-aware error messages
// ------------------------------------------------------------------------------------------------

struct Reader {
    std::string error;

    bool fail(const std::string& path, const std::string& message) {
        if (error.empty()) error = path + ": " + message;
        return false;
    }

    const Json* member(const Json& object, const std::string& path, const char* key, bool required = true) {
        const Json* v = object.find(key);
        if (v == nullptr && required) fail(path + "." + key, "missing");
        return v;
    }

    bool integer(const Json& object, const std::string& path, const char* key, long long& out, bool required = true) {
        const Json* v = member(object, path, key, required);
        if (v == nullptr) return !required;
        const std::string where = path + "." + key;
        if (v->type != Json::Type::Number) return fail(where, "expected a number");
        if (std::floor(v->number) != v->number || std::fabs(v->number) > 9007199254740992.0) {
            return fail(where, "expected an integer");
        }
        out = static_cast<long long>(v->number);
        return true;
    }

    bool narrowInt(const Json& object, const std::string& path, const char* key, int& out, bool required = true) {
        long long wide = 0;
        if (!integer(object, path, key, wide, required)) return false;
        if (object.find(key) == nullptr) return true;  // optional and absent
        if (wide < -1000000 || wide > 1000000) return fail(path + "." + key, "value out of range");
        out = static_cast<int>(wide);
        return true;
    }

    bool boolean(const Json& object, const std::string& path, const char* key, bool& out, bool required = true) {
        const Json* v = member(object, path, key, required);
        if (v == nullptr) return !required;
        if (v->type != Json::Type::Bool) return fail(path + "." + key, "expected true or false");
        out = v->boolean;
        return true;
    }

    template <typename Enum, std::size_t N>
    bool enumeration(const Json& object, const std::string& path, const char* key, Enum& out,
                     const std::pair<const char*, Enum> (&table)[N], bool required = true) {
        const Json* v = member(object, path, key, required);
        if (v == nullptr) return !required;
        const std::string where = path + "." + key;
        if (v->type != Json::Type::String) return fail(where, "expected a string");
        for (const auto& entry : table) {
            if (v->string == entry.first) {
                out = entry.second;
                return true;
            }
        }
        return fail(where, "unknown value '" + v->string + "'");
    }
};

const std::pair<const char*, ColourMapping> kMappings[] = {{"diatonic", ColourMapping::DiatonicCentric},
                                                           {"major", ColourMapping::MajorCentric}};
const std::pair<const char*, PaletteId> kPalettes[] = {{"classic", PaletteId::Classic},
                                                       {"soft", PaletteId::Soft},
                                                       {"colourBlindSafe", PaletteId::ColourBlindSafe}};
const std::pair<const char*, LabelMode> kLabelModes[] = {{"none", LabelMode::None},
                                                         {"pitch", LabelMode::Pitch},
                                                         {"degree", LabelMode::Degree},
                                                         {"both", LabelMode::Both}};

const std::pair<const char*, RegisterPreset> kRegisters[] = {{"low", RegisterPreset::Low}, {"mid", RegisterPreset::Mid}, {"high", RegisterPreset::High},
                                                             {"full", RegisterPreset::Full}, {"custom", RegisterPreset::Custom}};
const std::pair<const char*, Distribution> kDistributions[] = {{"automatic", Distribution::Automatic}, {"close", Distribution::Close},
                                                               {"open", Distribution::Open}, {"spread", Distribution::Spread}};

const std::pair<const char*, MidiFilenameFormat> kNamingFormats[] = {
    {"absolute", MidiFilenameFormat::Absolute}, {"roman", MidiFilenameFormat::Roman}, {"both", MidiFilenameFormat::Both}};
const std::pair<const char*, DiceMode> kDiceModes[] = {{"preserveRoot", DiceMode::PreserveRoot}, {"replaceHarmony", DiceMode::ReplaceHarmony}};
const std::pair<const char*, DiceComplexity> kDiceComplexities[] = {{"conservative", DiceComplexity::Conservative},
                                                                    {"adventurous", DiceComplexity::Adventurous},
                                                                    {"unrestricted", DiceComplexity::Unrestricted}};
const std::pair<const char*, ModeFilter> kModeFilters[] = {
    {"any", ModeFilter::Any},           {"ionian", ModeFilter::Ionian},         {"dorian", ModeFilter::Dorian},
    {"phrygian", ModeFilter::Phrygian}, {"lydian", ModeFilter::Lydian},         {"mixolydian", ModeFilter::Mixolydian},
    {"aeolian", ModeFilter::Aeolian},   {"locrian", ModeFilter::Locrian},       {"harmonicMinor", ModeFilter::HarmonicMinor},
    {"melodicMinor", ModeFilter::MelodicMinor}};

const std::pair<const char*, Contour> kContours[] = {{"ascending", Contour::Ascending}, {"descending", Contour::Descending}, {"arch", Contour::Arch},
                                                     {"invertedArch", Contour::InvertedArch}, {"wave", Contour::Wave}, {"free", Contour::Free}};
const std::pair<const char*, Density> kDensities[] = {{"sparse", Density::Sparse}, {"moderate", Density::Moderate}, {"dense", Density::Dense}};
const std::pair<const char*, RhythmStyle> kRhythms[] = {{"sustained", RhythmStyle::Sustained}, {"lyrical", RhythmStyle::Lyrical},
                                                        {"rhythmic", RhythmStyle::Rhythmic}, {"syncopated", RhythmStyle::Syncopated},
                                                        {"free", RhythmStyle::Free}};
const std::pair<const char*, PhraseScope> kScopes[] = {{"1bar", PhraseScope::OneBar}, {"2bars", PhraseScope::TwoBars}, {"4bars", PhraseScope::FourBars},
                                                       {"8bars", PhraseScope::EightBars}, {"selected", PhraseScope::SelectedRegion},
                                                       {"full", PhraseScope::FullProgression}};

template <typename Enum, std::size_t N>
const char* nameOf(Enum value, const std::pair<const char*, Enum> (&table)[N]) {
    for (const auto& entry : table) {
        if (entry.second == value) return entry.first;
    }
    return table[0].first;
}

// Chord Density settings, only the fields that differ from the defaults.
std::string voicingJson(const VoicingSettings& v) {
    const VoicingSettings d;
    std::string fields;
    auto add = [&fields](const std::string& f) {
        if (!fields.empty()) fields += ",";
        fields += f;
    };
    if (v.density != d.density) add("\"density\":" + std::to_string(v.density));
    if (v.registerPreset != d.registerPreset) add(std::string("\"register\":\"") + nameOf(v.registerPreset, kRegisters) + "\"");
    if (v.customLow != d.customLow) add("\"customLow\":" + std::to_string(v.customLow));
    if (v.customHigh != d.customHigh) add("\"customHigh\":" + std::to_string(v.customHigh));
    if (v.distribution != d.distribution) add(std::string("\"distribution\":\"") + nameOf(v.distribution, kDistributions) + "\"");
    if (v.preserveBass != d.preserveBass) add(std::string("\"preserveBass\":") + (v.preserveBass ? "true" : "false"));
    if (v.voiceLeading != d.voiceLeading) add(std::string("\"voiceLeading\":") + (v.voiceLeading ? "true" : "false"));
    return "{" + fields + "}";
}

std::string diceJson(const DiceSettings& v) {
    const DiceSettings d;
    std::string fields;
    auto add = [&fields](const std::string& f) {
        if (!fields.empty()) fields += ",";
        fields += f;
    };
    if (v.mode != d.mode) add(std::string("\"mode\":\"") + nameOf(v.mode, kDiceModes) + "\"");
    if (v.complexity != d.complexity) add(std::string("\"complexity\":\"") + nameOf(v.complexity, kDiceComplexities) + "\"");
    if (v.sourceFilter != d.sourceFilter) add(std::string("\"sourceMode\":\"") + nameOf(v.sourceFilter, kModeFilters) + "\"");
    if (v.avoidRepeats != d.avoidRepeats) add(std::string("\"avoidRepeats\":") + (v.avoidRepeats ? "true" : "false"));
    if (v.rootLock != d.rootLock) add(std::string("\"rootLock\":") + (v.rootLock ? "true" : "false"));
    if (v.seed != d.seed) add("\"seed\":" + std::to_string(v.seed));
    if (v.rollIndex != d.rollIndex) add("\"rollIndex\":" + std::to_string(v.rollIndex));
    return "{" + fields + "}";
}

bool readDice(Reader& r, const Json& object, const std::string& path, DiceSettings& v) {
    if (object.type != Json::Type::Object) return r.fail(path, "expected an object");
    long long seed = v.seed, roll = v.rollIndex;
    if (!r.enumeration(object, path, "mode", v.mode, kDiceModes, false) || !r.enumeration(object, path, "complexity", v.complexity, kDiceComplexities, false) ||
        !r.enumeration(object, path, "sourceMode", v.sourceFilter, kModeFilters, false) || !r.boolean(object, path, "avoidRepeats", v.avoidRepeats, false) ||
        !r.boolean(object, path, "rootLock", v.rootLock, false) || !r.integer(object, path, "seed", seed, false) ||
        !r.integer(object, path, "rollIndex", roll, false)) {
        return false;
    }
    if (seed < 0 || seed > 4294967295LL) return r.fail(path + ".seed", "out of range");
    if (roll < 0 || roll > 4294967295LL) return r.fail(path + ".rollIndex", "out of range");
    v.seed = static_cast<std::uint32_t>(seed);
    v.rollIndex = static_cast<std::uint32_t>(roll);
    return true;
}

bool readVoicing(Reader& r, const Json& object, const std::string& path, VoicingSettings& v) {
    if (object.type != Json::Type::Object) return r.fail(path, "expected an object");
    return r.narrowInt(object, path, "density", v.density, false) &&
           r.enumeration(object, path, "register", v.registerPreset, kRegisters, false) &&
           r.narrowInt(object, path, "customLow", v.customLow, false) && r.narrowInt(object, path, "customHigh", v.customHigh, false) &&
           r.enumeration(object, path, "distribution", v.distribution, kDistributions, false) &&
           r.boolean(object, path, "preserveBass", v.preserveBass, false) && r.boolean(object, path, "voiceLeading", v.voiceLeading, false);
}

const std::pair<const char*, Mode> kModes[] = {{"major", Mode::Major}, {"minor", Mode::Minor}};
const std::pair<const char*, Quality> kQualities[] = {{"major", Quality::Major},
                                                      {"minor", Quality::Minor},
                                                      {"diminished", Quality::Diminished},
                                                      {"augmented", Quality::Augmented}};
const std::pair<const char*, Seventh> kSevenths[] = {{"none", Seventh::None},
                                                     {"minor", Seventh::Minor},
                                                     {"major", Seventh::Major},
                                                     {"diminished", Seventh::Diminished}};
const std::pair<const char*, Ninth> kNinths[] = {{"none", Ninth::None}, {"minor", Ninth::Minor}, {"major", Ninth::Major}, {"sharp", Ninth::Sharp}};
const std::pair<const char*, Eleventh> kElevenths[] = {{"none", Eleventh::None}, {"perfect", Eleventh::Perfect}, {"sharp", Eleventh::Sharp}};
const std::pair<const char*, Thirteenth> kThirteenths[] = {{"none", Thirteenth::None}, {"minor", Thirteenth::Minor}, {"major", Thirteenth::Major}};
const std::pair<const char*, Suspension> kSuspensions[] = {{"none", Suspension::None},
                                                           {"sus2", Suspension::Sus2},
                                                           {"sus4", Suspension::Sus4}};

}  // namespace

std::string serializeProgression(const Progression& p) {
    const auto errors = p.validate();
    if (!errors.empty()) throw std::invalid_argument("cannot serialise an invalid progression: " + errors.front());

    std::string out;
    out += "{\"format\":\"chordsketch-state\",\"version\":" + std::to_string(kStateVersion);
    out += ",\"key\":{\"tonic\":" + std::to_string(p.key.tonic) + ",\"mode\":\"" + toString(p.key.mode) + "\"}";
    out += ",\"tempoBpm\":" + number(p.tempoBpm);
    out += ",\"timeSignature\":{\"numerator\":" + std::to_string(p.timeSignature.numerator) +
           ",\"denominator\":" + std::to_string(p.timeSignature.denominator) + "}";
    // Optional settings are written only when they differ from the defaults, so a project that does not use them
    // is byte-identical to a version-1 file written before they existed (and still opens in older builds).
    if (p.octave != 0) out += ",\"octave\":" + std::to_string(p.octave);
    if (p.lengthBars != 0) out += ",\"lengthBars\":" + std::to_string(p.lengthBars);
    if (p.exportNaming != MidiFilenameFormat::Absolute) out += std::string(",\"exportNaming\":\"") + nameOf(p.exportNaming, kNamingFormats) + "\"";
    if (p.dice != DiceSettings{}) out += ",\"dice\":" + diceJson(p.dice);
    if (p.voicing != VoicingSettings{}) out += ",\"voicing\":" + voicingJson(p.voicing);
    out += ",\"blocks\":[";
    for (std::size_t i = 0; i < p.blocks.size(); ++i) {
        const auto& b = p.blocks[i];
        if (i > 0) out += ",";
        out += "{\"degree\":" + std::to_string(b.chord.degree);
        out += ",\"accidental\":" + std::to_string(b.chord.accidental);
        out += std::string(",\"quality\":\"") + toString(b.chord.quality) + "\"";
        out += std::string(",\"seventh\":\"") + toString(b.chord.seventh) + "\"";
        out += ",\"inversion\":" + std::to_string(b.chord.inversion);
        out += std::string(",\"suspension\":\"") + toString(b.chord.suspension) + "\"";
        if (b.chord.ninth != Ninth::None) out += std::string(",\"ninth\":\"") + toString(b.chord.ninth) + "\"";
        if (b.chord.eleventh != Eleventh::None) out += std::string(",\"eleventh\":\"") + toString(b.chord.eleventh) + "\"";
        if (b.chord.thirteenth != Thirteenth::None) out += std::string(",\"thirteenth\":\"") + toString(b.chord.thirteenth) + "\"";
        out += ",\"startTick\":" + std::to_string(b.startTick);
        out += ",\"durationTicks\":" + std::to_string(b.durationTicks);
        if (b.octave != 0) out += ",\"octave\":" + std::to_string(b.octave);
        if (b.voicing) out += ",\"voicing\":" + voicingJson(*b.voicing);
        out += "}";
    }
    out += "]";
    // Melody lane and its settings: written only when used, so an old-style project stays byte-identical.
    if (!p.melody.empty()) {
        out += ",\"melody\":[";
        for (std::size_t i = 0; i < p.melody.size(); ++i) {
            const auto& n = p.melody[i];
            if (i > 0) out += ",";
            out += "{\"pitch\":" + std::to_string(n.pitch) + ",\"startTick\":" + std::to_string(n.startTick) +
                   ",\"durationTicks\":" + std::to_string(n.durationTicks) + ",\"velocity\":" + std::to_string(n.velocity);
            if (n.locked) out += ",\"locked\":true";
            out += "}";
        }
        out += "]";
    }
    if (p.melodySettings != MelodySettings{}) {
        const auto& m = p.melodySettings;
        const MelodySettings d;
        out += ",\"melodySettings\":{";
        std::string fields;
        auto add = [&fields](const std::string& f) {
            if (!fields.empty()) fields += ",";
            fields += f;
        };
        if (m.mapping != d.mapping) add(std::string("\"mapping\":\"") + toString(m.mapping) + "\"");
        if (m.palette != d.palette) add(std::string("\"palette\":\"") + toString(m.palette) + "\"");
        if (m.labels != d.labels) add(std::string("\"labels\":\"") + toString(m.labels) + "\"");
        if (m.chordChannel != d.chordChannel) add("\"chordChannel\":" + std::to_string(m.chordChannel));
        if (m.melodyChannel != d.melodyChannel) add("\"melodyChannel\":" + std::to_string(m.melodyChannel));
        if (m.muteChords) add("\"muteChords\":true");
        if (m.muteMelody) add("\"muteMelody\":true");
        if (!m.scaleLimiter) add("\"scaleLimiter\":false");  // ON is the default: absent = ON, so older projects load unchanged
        if (m.generator != d.generator) {
            const auto& g = m.generator;
            const auto& dg = d.generator;
            std::string gf;
            auto gadd = [&gf](const std::string& f) {
                if (!gf.empty()) gf += ",";
                gf += f;
            };
            if (g.contour != dg.contour) gadd(std::string("\"contour\":\"") + nameOf(g.contour, kContours) + "\"");
            if (g.density != dg.density) gadd(std::string("\"density\":\"") + nameOf(g.density, kDensities) + "\"");
            if (g.tension != dg.tension) gadd("\"tension\":" + std::to_string(g.tension));
            if (g.rhythm != dg.rhythm) gadd(std::string("\"rhythm\":\"") + nameOf(g.rhythm, kRhythms) + "\"");
            if (g.scope != dg.scope) gadd(std::string("\"scope\":\"") + nameOf(g.scope, kScopes) + "\"");
            if (g.registerLow != dg.registerLow) gadd("\"registerLow\":" + std::to_string(g.registerLow));
            if (g.registerHigh != dg.registerHigh) gadd("\"registerHigh\":" + std::to_string(g.registerHigh));
            if (g.seed != dg.seed) gadd("\"seed\":" + std::to_string(g.seed));
            add("\"generator\":{" + gf + "}");
        }
        out += fields + "}";
    }
    if (!p.melodyBasis.empty()) {
        out += ",\"melodyBasis\":[";
        for (std::size_t i = 0; i < p.melodyBasis.size(); ++i) {
            const auto& e = p.melodyBasis[i];
            if (i > 0) out += ",";
            out += "[" + std::to_string(e.startTick) + "," + std::to_string(e.durationTicks) + "," + std::to_string(e.chordHash) + "]";
        }
        out += "]";
    }
    out += "}";
    return out;
}

DeserializeResult deserializeProgression(std::string_view text) {
    DeserializeResult result;
    auto fail = [&result](const std::string& message) {
        result.ok = false;
        result.progression = Progression{};
        result.error = message;
        return result;
    };

    Json root;
    std::string parseError;
    if (!JsonParser(text).parse(root, parseError)) return fail("not valid JSON: " + parseError);
    if (root.type != Json::Type::Object) return fail("not a ChordSketch state: expected a JSON object");

    Reader r;

    const Json* format = root.find("format");
    if (format == nullptr || format->type != Json::Type::String || format->string != "chordsketch-state") {
        return fail("not a ChordSketch state (missing or wrong \"format\")");
    }

    long long version = 0;
    if (!r.integer(root, "$", "version", version)) return fail(r.error);
    if (version < 1) return fail("$.version: must be 1 or higher");
    if (version > kStateVersion) {
        return fail("saved by a newer version of ChordSketch (state version " + std::to_string(version) +
                    ", this build reads up to " + std::to_string(kStateVersion) + ")");
    }

    Progression p;

    const Json* key = r.member(root, "$", "key");
    if (key == nullptr) return fail(r.error);
    if (key->type != Json::Type::Object) return fail("$.key: expected an object");
    if (!r.narrowInt(*key, "$.key", "tonic", p.key.tonic) || !r.enumeration(*key, "$.key", "mode", p.key.mode, kModes)) {
        return fail(r.error);
    }

    const Json* tempo = r.member(root, "$", "tempoBpm");
    if (tempo == nullptr) return fail(r.error);
    if (tempo->type != Json::Type::Number) return fail("$.tempoBpm: expected a number");
    p.tempoBpm = tempo->number;

    const Json* ts = r.member(root, "$", "timeSignature");
    if (ts == nullptr) return fail(r.error);
    if (ts->type != Json::Type::Object) return fail("$.timeSignature: expected an object");
    if (!r.narrowInt(*ts, "$.timeSignature", "numerator", p.timeSignature.numerator) ||
        !r.narrowInt(*ts, "$.timeSignature", "denominator", p.timeSignature.denominator)) {
        return fail(r.error);
    }

    if (!r.narrowInt(root, "$", "octave", p.octave, /*required=*/false) ||
        !r.narrowInt(root, "$", "lengthBars", p.lengthBars, /*required=*/false)) {
        return fail(r.error);
    }

    if (const Json* v = r.member(root, "$", "exportNaming", /*required=*/false)) {
        (void)v;
        if (!r.enumeration(root, "$", "exportNaming", p.exportNaming, kNamingFormats, false)) return fail(r.error);
    }
    if (const Json* v = r.member(root, "$", "dice", /*required=*/false)) {
        if (!readDice(r, *v, "$.dice", p.dice)) return fail(r.error);
    }
    if (const Json* v = r.member(root, "$", "voicing", /*required=*/false)) {
        if (!readVoicing(r, *v, "$.voicing", p.voicing)) return fail(r.error);
    }

    const Json* blocks = r.member(root, "$", "blocks");
    if (blocks == nullptr) return fail(r.error);
    if (blocks->type != Json::Type::Array) return fail("$.blocks: expected an array");

    for (std::size_t i = 0; i < blocks->array.size(); ++i) {
        const Json& b = blocks->array[i];
        const std::string path = "$.blocks[" + std::to_string(i) + "]";
        if (b.type != Json::Type::Object) return fail(path + ": expected an object");

        ChordBlock block;
        long long start = 0, duration = 0;
        if (!r.narrowInt(b, path, "degree", block.chord.degree) ||
            !r.narrowInt(b, path, "accidental", block.chord.accidental) ||
            !r.enumeration(b, path, "quality", block.chord.quality, kQualities) ||
            !r.enumeration(b, path, "seventh", block.chord.seventh, kSevenths) ||
            !r.narrowInt(b, path, "inversion", block.chord.inversion) ||
            !r.enumeration(b, path, "suspension", block.chord.suspension, kSuspensions, /*required=*/false) ||
            !r.enumeration(b, path, "ninth", block.chord.ninth, kNinths, /*required=*/false) ||
            !r.enumeration(b, path, "eleventh", block.chord.eleventh, kElevenths, /*required=*/false) ||
            !r.enumeration(b, path, "thirteenth", block.chord.thirteenth, kThirteenths, /*required=*/false) ||
            !r.integer(b, path, "startTick", start) || !r.integer(b, path, "durationTicks", duration) ||
            !r.narrowInt(b, path, "octave", block.octave, /*required=*/false)) {
            return fail(r.error);
        }
        block.startTick = start;
        block.durationTicks = duration;
        if (const Json* v = r.member(b, path, "voicing", /*required=*/false)) {
            VoicingSettings settings;
            if (!readVoicing(r, *v, path + ".voicing", settings)) return fail(r.error);
            block.voicing = settings;
        }
        p.blocks.push_back(block);
    }

    if (const Json* melody = r.member(root, "$", "melody", /*required=*/false)) {
        if (melody->type != Json::Type::Array) return fail("$.melody: expected an array");
        if (melody->array.size() > static_cast<std::size_t>(kMaxMelodyNotes)) return fail("$.melody: too many notes");
        for (std::size_t i = 0; i < melody->array.size(); ++i) {
            const Json& m = melody->array[i];
            const std::string path = "$.melody[" + std::to_string(i) + "]";
            if (m.type != Json::Type::Object) return fail(path + ": expected an object");
            MelodyNote note;
            long long start = 0, duration = 0;
            if (!r.narrowInt(m, path, "pitch", note.pitch) || !r.integer(m, path, "startTick", start) ||
                !r.integer(m, path, "durationTicks", duration) || !r.narrowInt(m, path, "velocity", note.velocity) ||
                !r.boolean(m, path, "locked", note.locked, /*required=*/false)) {
                return fail(r.error);
            }
            note.startTick = start;
            note.durationTicks = duration;
            p.melody.push_back(note);
        }
    }
    if (const Json* ms = r.member(root, "$", "melodySettings", /*required=*/false)) {
        if (ms->type != Json::Type::Object) return fail("$.melodySettings: expected an object");
        auto& s = p.melodySettings;
        if (!r.enumeration(*ms, "$.melodySettings", "mapping", s.mapping, kMappings, false) ||
            !r.enumeration(*ms, "$.melodySettings", "palette", s.palette, kPalettes, false) ||
            !r.enumeration(*ms, "$.melodySettings", "labels", s.labels, kLabelModes, false) ||
            !r.narrowInt(*ms, "$.melodySettings", "chordChannel", s.chordChannel, false) ||
            !r.narrowInt(*ms, "$.melodySettings", "melodyChannel", s.melodyChannel, false) ||
            !r.boolean(*ms, "$.melodySettings", "muteChords", s.muteChords, false) ||
            !r.boolean(*ms, "$.melodySettings", "muteMelody", s.muteMelody, false) ||
            !r.boolean(*ms, "$.melodySettings", "scaleLimiter", s.scaleLimiter, false)) {
            return fail(r.error);
        }
        if (const Json* gen = r.member(*ms, "$.melodySettings", "generator", /*required=*/false)) {
            if (gen->type != Json::Type::Object) return fail("$.melodySettings.generator: expected an object");
            auto& g = s.generator;
            const std::string gp = "$.melodySettings.generator";
            long long seed = g.seed;
            if (!r.enumeration(*gen, gp, "contour", g.contour, kContours, false) || !r.enumeration(*gen, gp, "density", g.density, kDensities, false) ||
                !r.narrowInt(*gen, gp, "tension", g.tension, false) || !r.enumeration(*gen, gp, "rhythm", g.rhythm, kRhythms, false) ||
                !r.enumeration(*gen, gp, "scope", g.scope, kScopes, false) || !r.narrowInt(*gen, gp, "registerLow", g.registerLow, false) ||
                !r.narrowInt(*gen, gp, "registerHigh", g.registerHigh, false) || !r.integer(*gen, gp, "seed", seed, false)) {
                return fail(r.error);
            }
            if (seed < 0 || seed > 4294967295LL) return fail(gp + ".seed: out of range");
            g.seed = static_cast<std::uint32_t>(seed);
        }
    }
    if (const Json* basis = r.member(root, "$", "melodyBasis", /*required=*/false)) {
        if (basis->type != Json::Type::Array) return fail("$.melodyBasis: expected an array");
        if (basis->array.size() > static_cast<std::size_t>(kMaxMelodyNotes)) return fail("$.melodyBasis: too many entries");
        for (std::size_t i = 0; i < basis->array.size(); ++i) {
            const Json& e = basis->array[i];
            const std::string path = "$.melodyBasis[" + std::to_string(i) + "]";
            if (e.type != Json::Type::Array || e.array.size() != 3) return fail(path + ": expected [start, duration, hash]");
            MelodyBasisEntry entry;
            for (int k = 0; k < 3; ++k) {
                const Json& v = e.array[static_cast<std::size_t>(k)];
                if (v.type != Json::Type::Number || std::floor(v.number) != v.number || v.number < 0 || v.number > 9007199254740992.0) {
                    return fail(path + ": expected non-negative integers");
                }
            }
            entry.startTick = static_cast<Tick>(e.array[0].number);
            entry.durationTicks = static_cast<Tick>(e.array[1].number);
            if (e.array[2].number > 4294967295.0) return fail(path + ": hash out of range");
            entry.chordHash = static_cast<std::uint32_t>(e.array[2].number);
            p.melodyBasis.push_back(entry);
        }
    }

    const auto problems = p.validate();
    if (!problems.empty()) return fail("invalid progression: " + problems.front());

    result.ok = true;
    result.progression = std::move(p);
    result.error.clear();
    return result;
}

}  // namespace chordcore
