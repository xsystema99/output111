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
        const auto colon = token.rfind(':');
        if (colon != std::string_view::npos) {
            symbol = token.substr(0, colon);
            const std::string_view lengthText = token.substr(colon + 1);
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
        result.blocks.push_back(ChordBlock{chord.chord, cursor, length});
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
        append(token);
        cursor = block.endTick();
    }
    return out;
}

}  // namespace chordcore
