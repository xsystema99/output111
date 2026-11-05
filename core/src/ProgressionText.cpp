#include <chordcore/ProgressionText.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <system_error>

#include <chordcore/RomanNumeral.h>

namespace chordcore {

namespace {

constexpr std::size_t kMaxTokens = 256;
constexpr double kMaxBars = 64.0;

bool isSeparator(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == ',' || c == '|'; }

std::string barsText(Tick ticks, Tick ticksPerBar) {
    if (ticks % ticksPerBar == 0) return std::to_string(ticks / ticksPerBar);
    char buffer[64];
    const auto r = std::to_chars(buffer, buffer + sizeof buffer, static_cast<double>(ticks) / static_cast<double>(ticksPerBar));
    return std::string(buffer, r.ptr);
}

}  // namespace

ParseProgressionResult parseProgressionText(std::string_view text, const TimeSignature& ts) {
    ParseProgressionResult result;
    auto fail = [&result](std::string message) {
        result.ok = false;
        result.blocks.clear();
        result.error = std::move(message);
        return result;
    };
    if (!ts.isValid()) return fail("invalid time signature");
    const Tick bar = ts.ticksPerBar();

    Tick cursor = 0;
    std::size_t tokens = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        if (isSeparator(text[i])) { ++i; continue; }
        std::size_t j = i;
        while (j < text.size() && !isSeparator(text[j])) ++j;
        const std::string_view token = text.substr(i, j - i);
        i = j;

        if (++tokens > kMaxTokens) return fail("too many chords (the limit is 256)");

        std::string_view symbol = token;
        Tick length = bar;
        int octave = 0;
        std::string_view body = token;
        const auto at = token.rfind('@');
        if (at != std::string_view::npos) {  // optional chord octave shift: @+1, @-2
            std::string_view octaveText = token.substr(at + 1);
            body = token.substr(0, at);
            bool negative = false;
            if (!octaveText.empty() && (octaveText.front() == '+' || octaveText.front() == '-')) {
                negative = octaveText.front() == '-';
                octaveText.remove_prefix(1);
            }
            if (octaveText.size() != 1 || octaveText[0] < '0' || octaveText[0] > '0' + kMaxOctaveShift) {
                return fail("bad octave in '" + std::string(token) + "' (expected @-2 to @+2)");
            }
            octave = (octaveText[0] - '0') * (negative ? -1 : 1);
        }
        symbol = body;
        const auto colon = body.rfind(':');
        if (colon != std::string_view::npos) {
            symbol = body.substr(0, colon);
            const std::string_view lengthText = body.substr(colon + 1);
            double bars = 0.0;
            const auto r = std::from_chars(lengthText.data(), lengthText.data() + lengthText.size(), bars);
            if (lengthText.empty() || r.ec != std::errc() || r.ptr != lengthText.data() + lengthText.size() ||
                !std::isfinite(bars) || bars <= 0.0 || bars > kMaxBars) {
                return fail("bad length in '" + std::string(token) + "' (expected a number of bars above 0 and at most 64)");
            }
            length = static_cast<Tick>(std::llround(bars * static_cast<double>(bar)));
            if (length < 1) return fail("length too short in '" + std::string(token) + "'");
        }

        if (symbol == "-") {
            cursor += length;
            continue;
        }

        const auto chord = parseRomanNumeral(symbol);
        if (!chord.ok) return fail("'" + std::string(token) + "': " + chord.error);
        ChordBlock block{chord.chord, cursor, length};
        block.octave = octave;
        result.blocks.push_back(block);
        cursor += length;
    }

    result.ok = true;
    return result;
}

std::string formatProgressionText(const Progression& progression) {
    std::vector<ChordBlock> blocks = progression.blocks;
    std::stable_sort(blocks.begin(), blocks.end(),
                     [](const ChordBlock& a, const ChordBlock& b) { return a.startTick < b.startTick; });

    const Tick bar = progression.timeSignature.isValid() ? progression.timeSignature.ticksPerBar() : 3840;
    std::string out;
    auto append = [&out](const std::string& token) {
        if (!out.empty()) out += ' ';
        out += token;
    };

    Tick cursor = 0;
    for (const auto& block : blocks) {
        if (block.startTick > cursor) {
            const Tick gap = block.startTick - cursor;
            append(gap == bar ? "-" : "-:" + barsText(gap, bar));
        }
        const auto symbol = formatRomanNumeral(block.chord);
        std::string token = symbol ? *symbol : std::string("?");
        if (block.durationTicks != bar) token += ":" + barsText(block.durationTicks, bar);
        if (block.octave != 0) token += std::string("@") + (block.octave > 0 ? "+" : "-") + std::to_string(std::abs(block.octave));
        append(token);
        cursor = block.endTick();
    }
    return out;
}

}  // namespace chordcore
