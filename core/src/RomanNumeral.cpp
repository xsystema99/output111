#include <chordcore/RomanNumeral.h>

#include <string>
#include <string_view>
#include <utility>

namespace chordcore {

namespace {

struct NumeralToken {
    const char* text;
    int degree;
};

// Longest tokens first so "VII" is not read as "VI" + "I".
constexpr NumeralToken kUpper[] = {{"VII", 7}, {"III", 3}, {"VI", 6}, {"IV", 4}, {"II", 2}, {"V", 5}, {"I", 1}};
constexpr NumeralToken kLower[] = {{"vii", 7}, {"iii", 3}, {"vi", 6}, {"iv", 4}, {"ii", 2}, {"v", 5}, {"i", 1}};

struct Figure {
    const char* text;
    bool seventh;
    int inversion;
    bool majorSeventh;
};

constexpr Figure kFigures[] = {
    {"6", false, 1, false},   {"64", false, 2, false}, {"7", true, 0, false},
    {"65", true, 1, false},   {"43", true, 2, false},  {"42", true, 3, false},
    {"2", true, 3, false},    {"maj7", true, 0, true}, {"M7", true, 0, true},
};

bool consume(std::string_view& s, std::string_view prefix) {
    if (s.substr(0, prefix.size()) == prefix) {
        s.remove_prefix(prefix.size());
        return true;
    }
    return false;
}

}  // namespace

ParseResult parseRomanNumeral(std::string_view text) {
    ParseResult result;
    auto fail = [&result](std::string message) {
        result.ok = false;
        result.error = std::move(message);
        return result;
    };

    std::string_view s = text;
    if (s.empty()) return fail("empty chord symbol");

    ChordSpec chord;

    if (consume(s, "b") || consume(s, "\xE2\x99\xAD")) {
        chord.accidental = -1;
    } else if (consume(s, "#") || consume(s, "\xE2\x99\xAF")) {
        chord.accidental = +1;
    }

    bool upper = true;
    bool found = false;
    for (const auto& token : kUpper) {
        if (consume(s, token.text)) {
            chord.degree = token.degree;
            found = true;
            break;
        }
    }
    if (!found) {
        for (const auto& token : kLower) {
            if (consume(s, token.text)) {
                chord.degree = token.degree;
                upper = false;
                found = true;
                break;
            }
        }
    }
    if (!found) return fail("expected a Roman numeral (I-VII or i-vii) in '" + std::string(text) + "'");

    chord.quality = upper ? Quality::Major : Quality::Minor;

    bool halfDiminished = false;
    if (consume(s, "o") || consume(s, "\xC2\xB0")) {
        if (upper) return fail("diminished symbol requires a lowercase numeral in '" + std::string(text) + "'");
        chord.quality = Quality::Diminished;
    } else if (consume(s, "\xC3\xB8")) {
        if (upper) return fail("half-diminished symbol requires a lowercase numeral in '" + std::string(text) + "'");
        chord.quality = Quality::Diminished;
        halfDiminished = true;
    } else if (consume(s, "+")) {
        if (!upper) return fail("augmented symbol requires an uppercase numeral in '" + std::string(text) + "'");
        chord.quality = Quality::Augmented;
    }

    // Suspension suffix (checked before the figure so "7sus4" splits into "7" + "sus4").
    for (const auto& [suffix, value] : {std::pair<const char*, Suspension>{"sus2", Suspension::Sus2},
                                        {"sus4", Suspension::Sus4},
                                        {"sus", Suspension::Sus4}}) {
        const std::string_view sv(suffix);
        if (s.size() >= sv.size() && s.substr(s.size() - sv.size()) == sv) {
            s.remove_suffix(sv.size());
            chord.suspension = value;
            break;
        }
    }
    if (chord.suspension != Suspension::None &&
        (halfDiminished || chord.quality == Quality::Diminished || chord.quality == Quality::Augmented)) {
        return fail("a suspension replaces the third and cannot be combined with o, half-diminished or + in '" +
                    std::string(text) + "'");
    }

    bool seventh = halfDiminished;
    bool majorSeventh = false;
    if (!s.empty()) {
        const Figure* match = nullptr;
        for (const auto& figure : kFigures) {
            if (s == figure.text) {
                match = &figure;
                break;
            }
        }
        if (match == nullptr) return fail("unsupported or unrecognised suffix '" + std::string(s) + "' in '" + std::string(text) + "'");
        if (halfDiminished && (!match->seventh || match->majorSeventh)) {
            return fail("half-diminished chords are minor-seventh chords; use 7-chord figures (65, 43, 42) in '" + std::string(text) + "'");
        }
        seventh = match->seventh;
        majorSeventh = match->majorSeventh;
        chord.inversion = match->inversion;
    }

    if (!seventh) {
        chord.seventh = Seventh::None;
    } else if (majorSeventh) {
        chord.seventh = Seventh::Major;
    } else if (chord.quality == Quality::Diminished && !halfDiminished) {
        chord.seventh = Seventh::Diminished;  // fully diminished seventh (e.g. viio7)
    } else {
        chord.seventh = Seventh::Minor;       // V7 (dominant), i7, half-diminished, augmented 7
    }

    if (!chord.isValid()) return fail("invalid chord '" + std::string(text) + "'");

    result.ok = true;
    result.chord = chord;
    result.error.clear();
    return result;
}

}  // namespace chordcore
