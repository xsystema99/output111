#include "SmfParser.h"

#include <algorithm>
#include <map>
#include <utility>

namespace smf {

namespace {

struct Reader {
    const std::vector<std::uint8_t>& bytes;
    std::size_t pos;
    std::size_t limit;

    std::uint8_t u8() {
        if (pos >= limit) throw std::runtime_error("unexpected end of data at byte " + std::to_string(pos));
        return bytes[pos++];
    }
    std::uint32_t u16() {
        const std::uint32_t hi = u8();
        return (hi << 8) | u8();
    }
    std::uint32_t u32() {
        const std::uint32_t hi = u16();
        return (hi << 16) | u16();
    }
    std::uint32_t vlq() {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const std::uint8_t b = u8();
            value = (value << 7) | (b & 0x7Fu);
            if ((b & 0x80u) == 0) return value;
        }
        throw std::runtime_error("variable-length quantity longer than 4 bytes");
    }
};

std::string tag(Reader& r) {
    std::string s;
    for (int i = 0; i < 4; ++i) s.push_back(static_cast<char>(r.u8()));
    return s;
}

int dataByteCount(std::uint8_t status) {
    switch (status & 0xF0) {
        case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
        case 0xC0: case 0xD0: return 1;
        default: throw std::runtime_error("unsupported status byte");
    }
}

Track parseTrack(Reader& r) {
    Track track;
    std::uint64_t tick = 0;
    std::uint8_t runningStatus = 0;
    bool sawEnd = false;

    while (r.pos < r.limit) {
        if (sawEnd) throw std::runtime_error("data after End-of-Track");

        tick += r.vlq();
        Event e;
        e.tick = tick;
        e.fileOrder = track.events.size();

        std::uint8_t first = r.u8();
        if (first == 0xFF) {
            e.isMeta = true;
            e.metaType = r.u8();
            const std::uint32_t len = r.vlq();
            for (std::uint32_t i = 0; i < len; ++i) e.data.push_back(r.u8());
            if (e.metaType == 0x2F) {
                if (len != 0) throw std::runtime_error("End-of-Track must have zero length");
                sawEnd = true;
            }
            runningStatus = 0;  // meta events cancel running status
        } else if (first == 0xF0 || first == 0xF7) {
            throw std::runtime_error("SysEx not expected in chordcore output");
        } else {
            std::uint8_t status;
            if (first & 0x80) {
                status = first;
                runningStatus = status;
                e.status = status;
                const int n = dataByteCount(status);
                for (int i = 0; i < n; ++i) e.data.push_back(r.u8());
            } else {
                if (runningStatus == 0) throw std::runtime_error("data byte without status or running status");
                track.usedRunningStatus = true;
                status = runningStatus;
                e.status = status;
                e.data.push_back(first);
                const int n = dataByteCount(status);
                for (int i = 1; i < n; ++i) e.data.push_back(r.u8());
            }
            for (auto b : e.data) {
                if (b & 0x80) throw std::runtime_error("data byte with high bit set");
            }
        }
        track.events.push_back(std::move(e));
    }

    if (!sawEnd) throw std::runtime_error("track missing End-of-Track");
    return track;
}

}  // namespace

File parse(const std::vector<std::uint8_t>& bytes) {
    Reader r{bytes, 0, bytes.size()};
    File file;

    if (tag(r) != "MThd") throw std::runtime_error("missing MThd");
    if (r.u32() != 6) throw std::runtime_error("MThd length must be 6");
    file.format = static_cast<int>(r.u16());
    file.trackCount = static_cast<int>(r.u16());
    const std::uint32_t division = r.u16();
    if (division & 0x8000u) throw std::runtime_error("SMPTE division not supported");
    if (division == 0) throw std::runtime_error("division must be > 0");
    file.division = static_cast<int>(division);
    if (file.format < 0 || file.format > 2) throw std::runtime_error("unknown SMF format");
    if (file.format == 0 && file.trackCount != 1) throw std::runtime_error("format 0 must have exactly one track");

    for (int t = 0; t < file.trackCount; ++t) {
        if (tag(r) != "MTrk") throw std::runtime_error("missing MTrk");
        const std::uint32_t len = r.u32();
        if (len > bytes.size() - r.pos) throw std::runtime_error("MTrk length exceeds file size");
        Reader sub{bytes, r.pos, r.pos + len};
        file.tracks.push_back(parseTrack(sub));
        r.pos += len;
    }
    if (r.pos != bytes.size()) throw std::runtime_error("trailing bytes after last track");
    return file;
}

std::vector<Note> extractNotes(const Track& track) {
    std::vector<Note> notes;
    // (channel, pitch) -> stack of open notes (FIFO would also be valid; chordcore never overlaps same pitch)
    std::map<std::pair<int, int>, std::vector<Note>> open;

    for (const auto& e : track.events) {
        if (e.isMeta) continue;
        const bool on = e.kind() == 0x90 && e.data[1] != 0;
        const bool off = e.kind() == 0x80 || (e.kind() == 0x90 && e.data[1] == 0);
        if (!on && !off) continue;
        const auto key = std::make_pair(e.channel(), static_cast<int>(e.data[0]));
        if (on) {
            Note n;
            n.pitch = e.data[0];
            n.start = e.tick;
            n.velocity = e.data[1];
            n.channel = e.channel();
            open[key].push_back(n);
        } else {
            auto& stack = open[key];
            if (stack.empty()) throw std::runtime_error("note-off without matching note-on");
            Note n = stack.front();
            stack.erase(stack.begin());
            n.length = e.tick - n.start;
            notes.push_back(n);
        }
    }
    for (const auto& kv : open) {
        if (!kv.second.empty()) throw std::runtime_error("note-on never released (stuck note)");
    }
    std::sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return a.start != b.start ? a.start < b.start : a.pitch < b.pitch;
    });
    return notes;
}

std::vector<const Event*> metaEvents(const Track& track, std::uint8_t metaType) {
    std::vector<const Event*> out;
    for (const auto& e : track.events) {
        if (e.isMeta && e.metaType == metaType) out.push_back(&e);
    }
    return out;
}

}  // namespace smf
