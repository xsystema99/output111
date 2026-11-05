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

const std::pair<const char*, Mode> kModes[] = {{"major", Mode::Major}, {"minor", Mode::Minor}};
const std::pair<const char*, Quality> kQualities[] = {{"major", Quality::Major},
                                                      {"minor", Quality::Minor},
                                                      {"diminished", Quality::Diminished},
                                                      {"augmented", Quality::Augmented}};
const std::pair<const char*, Seventh> kSevenths[] = {{"none", Seventh::None},
                                                     {"minor", Seventh::Minor},
                                                     {"major", Seventh::Major},
                                                     {"diminished", Seventh::Diminished}};
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
        out += ",\"startTick\":" + std::to_string(b.startTick);
        out += ",\"durationTicks\":" + std::to_string(b.durationTicks);
        if (b.octave != 0) out += ",\"octave\":" + std::to_string(b.octave);
        out += "}";
    }
    out += "]}";
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
            !r.integer(b, path, "startTick", start) || !r.integer(b, path, "durationTicks", duration) ||
            !r.narrowInt(b, path, "octave", block.octave, /*required=*/false)) {
            return fail(r.error);
        }
        block.startTick = start;
        block.durationTicks = duration;
        p.blocks.push_back(block);
    }

    const auto problems = p.validate();
    if (!problems.empty()) return fail("invalid progression: " + problems.front());

    result.ok = true;
    result.progression = std::move(p);
    result.error.clear();
    return result;
}

}  // namespace chordcore
