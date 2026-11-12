#include <chordcore/MelodyEdit.h>

#include <algorithm>
#include <numeric>
#include <string>

namespace chordcore {

namespace {

OpResult fail(std::string message) {
    OpResult r;
    r.ok = false;
    r.error = std::move(message);
    return r;
}

bool lessNote(const MelodyNote& a, const MelodyNote& b) {
    return a.startTick != b.startTick ? a.startTick < b.startTick : a.pitch < b.pitch;
}

/// Sorts the melody and rewrites `tracked` (old positions) to the new positions.
void sortMelody(Progression& p, std::vector<std::size_t>& tracked) {
    std::vector<std::size_t> order(p.melody.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&p](std::size_t a, std::size_t b) { return lessNote(p.melody[a], p.melody[b]); });
    std::vector<MelodyNote> sorted;
    sorted.reserve(order.size());
    std::vector<std::size_t> newPosition(order.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        sorted.push_back(p.melody[order[i]]);
        newPosition[order[i]] = i;
    }
    p.melody = std::move(sorted);
    for (auto& t : tracked) t = newPosition[t];
    std::sort(tracked.begin(), tracked.end());
}

bool overlapsSamePitch(const std::vector<MelodyNote>& notes, const MelodyNote& n, const std::vector<bool>& skip) {
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!skip.empty() && skip[i]) continue;
        const auto& o = notes[i];
        if (o.pitch == n.pitch && o.startTick < n.endTick() && n.startTick < o.endTick()) return true;
    }
    return false;
}

bool validIndices(const Progression& p, const std::vector<std::size_t>& indices, std::vector<bool>& mask) {
    if (indices.empty()) return false;
    mask.assign(p.melody.size(), false);
    for (auto i : indices) {
        if (i >= p.melody.size() || mask[i]) return false;
        mask[i] = true;
    }
    return true;
}

int clampVelocity(int v) { return std::max(1, std::min(127, v)); }

OpResult commitAdded(Progression& p, const std::vector<MelodyNote>& added, const Progression& original) {
    // `added` were appended to p.melody (positions original.size()..); re-sort and report the new positions.
    (void)original;
    std::vector<std::size_t> tracked;
    for (std::size_t i = p.melody.size() - added.size(); i < p.melody.size(); ++i) tracked.push_back(i);
    sortMelody(p, tracked);
    OpResult r;
    r.ok = true;
    r.indices = tracked;
    r.index = tracked.empty() ? 0 : tracked.front();
    return r;
}

}  // namespace

int findMelodyNoteAt(const Progression& p, Tick tick, int pitch) {
    for (std::size_t i = 0; i < p.melody.size(); ++i) {
        const auto& n = p.melody[i];
        if (n.pitch == pitch && n.startTick <= tick && tick < n.endTick()) return static_cast<int>(i);
    }
    return -1;
}

OpResult addMelodyNote(Progression& p, int pitch, Tick atTick, Tick unit, Tick duration, int velocity) {
    if (pitch < 0 || pitch > 127) return fail("pitch is outside the MIDI range");
    if (p.melody.size() >= static_cast<std::size_t>(kMaxMelodyNotes)) return fail("the melody lane is full");
    const Tick u = std::max<Tick>(unit, 1);
    const Tick start = std::max<Tick>(snapToUnit(atTick, u), 0);
    const Tick limit = maxEndTick(p.timeSignature);
    if (start >= limit) return fail("no room: past the end of the timeline");

    Tick length = duration > 0 ? duration : u;
    Tick end = std::min(start + length, limit);
    for (const auto& o : p.melody) {
        if (o.pitch != pitch) continue;
        if (o.startTick <= start && start < o.endTick()) return fail("a note of this pitch is already there");
        if (o.startTick > start) end = std::min(end, o.startTick);
    }
    if (end <= start) return fail("no room for a note here");

    MelodyNote n;
    n.pitch = pitch;
    n.startTick = start;
    n.durationTicks = end - start;
    n.velocity = clampVelocity(velocity);
    p.melody.push_back(n);
    return commitAdded(p, {n}, p);
}

OpResult moveMelodyNotes(Progression& p, const std::vector<std::size_t>& indices, std::size_t anchor,
                         Tick desiredAnchorStart, int pitchDelta, Tick unit) {
    std::vector<bool> mask;
    if (!validIndices(p, indices, mask)) return fail("invalid selection");
    if (anchor >= p.melody.size() || !mask[anchor]) return fail("invalid anchor");
    const Tick u = std::max<Tick>(unit, 1);
    const Tick limit = maxEndTick(p.timeSignature);

    Tick minStart = p.melody[indices.front()].startTick, maxEnd = 0;
    int minPitch = 127, maxPitch = 0;
    for (auto i : indices) {
        minStart = std::min(minStart, p.melody[i].startTick);
        maxEnd = std::max(maxEnd, p.melody[i].endTick());
        minPitch = std::min(minPitch, p.melody[i].pitch);
        maxPitch = std::max(maxPitch, p.melody[i].pitch);
    }
    Tick dt = snapToUnit(desiredAnchorStart, u) - p.melody[anchor].startTick;
    dt = std::max(dt, -minStart);
    dt = std::min(dt, limit - maxEnd);
    const int dp = std::max(-minPitch, std::min(127 - maxPitch, pitchDelta));

    std::vector<MelodyNote> moved = p.melody;
    for (auto i : indices) {
        moved[i].startTick += dt;
        moved[i].pitch += dp;
    }
    for (auto i : indices) {
        if (overlapsSamePitch(moved, moved[i], mask)) return fail("a moved note would overlap another note of the same pitch");
    }
    // Moved notes must also not overlap each other (a group moved rigidly cannot, unless it already did).
    p.melody = std::move(moved);
    std::vector<std::size_t> tracked = indices;
    sortMelody(p, tracked);
    OpResult r;
    r.ok = true;
    r.indices = tracked;
    r.index = tracked.empty() ? 0 : tracked.front();
    return r;
}

OpResult resizeMelodyNoteEnd(Progression& p, std::size_t index, Tick desiredEnd, Tick unit) {
    if (index >= p.melody.size()) return fail("invalid note");
    const Tick u = std::max<Tick>(unit, 1);
    auto& n = p.melody[index];
    Tick end = snapToUnit(desiredEnd, u);
    const Tick minLength = std::min(u, n.durationTicks);
    end = std::max(end, n.startTick + minLength);
    end = std::min(end, maxEndTick(p.timeSignature));
    for (std::size_t i = 0; i < p.melody.size(); ++i) {
        if (i == index || p.melody[i].pitch != n.pitch) continue;
        if (p.melody[i].startTick >= n.endTick()) end = std::min(end, p.melody[i].startTick);
    }
    end = std::max(end, n.startTick + 1);
    n.durationTicks = end - n.startTick;
    OpResult r;
    r.ok = true;
    r.index = index;
    r.indices = {index};
    return r;
}

OpResult resizeMelodyNoteStart(Progression& p, std::size_t index, Tick desiredStart, Tick unit) {
    if (index >= p.melody.size()) return fail("invalid note");
    const Tick u = std::max<Tick>(unit, 1);
    auto& n = p.melody[index];
    const Tick end = n.endTick();
    const Tick minLength = std::min(u, n.durationTicks);
    Tick start = snapToUnit(desiredStart, u);
    start = std::min(start, end - minLength);
    start = std::max<Tick>(start, 0);
    for (std::size_t i = 0; i < p.melody.size(); ++i) {
        if (i == index || p.melody[i].pitch != n.pitch) continue;
        if (p.melody[i].endTick() <= n.startTick) start = std::max(start, p.melody[i].endTick());
    }
    start = std::min(start, end - 1);
    n.startTick = start;
    n.durationTicks = end - start;
    std::vector<std::size_t> tracked{index};
    sortMelody(p, tracked);
    OpResult r;
    r.ok = true;
    r.index = tracked.front();
    r.indices = tracked;
    return r;
}

OpResult deleteMelodyNotes(Progression& p, const std::vector<std::size_t>& indices) {
    std::vector<bool> mask;
    if (!validIndices(p, indices, mask)) return fail("invalid selection");
    std::vector<MelodyNote> kept;
    for (std::size_t i = 0; i < p.melody.size(); ++i) {
        if (!mask[i]) kept.push_back(p.melody[i]);
    }
    p.melody = std::move(kept);
    OpResult r;
    r.ok = true;
    return r;
}

namespace {

OpResult appendGroup(Progression& p, const std::vector<MelodyNote>& group, Tick shift) {
    if (group.empty()) return fail("nothing to paste");
    if (p.melody.size() + group.size() > static_cast<std::size_t>(kMaxMelodyNotes)) return fail("the melody lane is full");
    const Tick limit = maxEndTick(p.timeSignature);
    std::vector<MelodyNote> placed;
    for (auto n : group) {
        n.startTick += shift;
        n.locked = false;
        n.velocity = clampVelocity(n.velocity);
        if (n.pitch < 0 || n.pitch > 127) return fail("pitch is outside the MIDI range");
        if (n.startTick < 0 || n.endTick() > limit) return fail("no room: the copy would go past the end of the timeline");
        if (overlapsSamePitch(p.melody, n, {})) return fail("the copy would overlap an existing note");
        for (const auto& other : placed) {
            if (other.pitch == n.pitch && other.startTick < n.endTick() && n.startTick < other.endTick()) {
                return fail("the copy would overlap itself");
            }
        }
        placed.push_back(n);
    }
    for (const auto& n : placed) p.melody.push_back(n);
    return commitAdded(p, placed, p);
}

}  // namespace

OpResult duplicateMelodyNotes(Progression& p, const std::vector<std::size_t>& indices) {
    std::vector<bool> mask;
    if (!validIndices(p, indices, mask)) return fail("invalid selection");
    std::vector<MelodyNote> group;
    Tick first = p.melody[indices.front()].startTick, last = 0;
    for (auto i : indices) {
        group.push_back(p.melody[i]);
        first = std::min(first, p.melody[i].startTick);
        last = std::max(last, p.melody[i].endTick());
    }
    return appendGroup(p, std::move(group), last - first);
}

OpResult pasteMelodyNotes(Progression& p, const std::vector<MelodyNote>& clip, Tick atTick, Tick unit) {
    if (clip.empty()) return fail("the melody clipboard is empty");
    Tick first = clip.front().startTick;
    for (const auto& n : clip) first = std::min(first, n.startTick);
    const Tick target = std::max<Tick>(snapToUnit(atTick, std::max<Tick>(unit, 1)), 0);
    return appendGroup(p, clip, target - first);
}

OpResult setMelodyVelocity(Progression& p, const std::vector<std::size_t>& indices, int velocity) {
    std::vector<bool> mask;
    if (!validIndices(p, indices, mask)) return fail("invalid selection");
    for (auto i : indices) p.melody[i].velocity = clampVelocity(velocity);
    OpResult r;
    r.ok = true;
    r.index = indices.front();
    r.indices = indices;
    return r;
}

OpResult setMelodyLocked(Progression& p, const std::vector<std::size_t>& indices, bool locked) {
    std::vector<bool> mask;
    if (!validIndices(p, indices, mask)) return fail("invalid selection");
    for (auto i : indices) p.melody[i].locked = locked;
    OpResult r;
    r.ok = true;
    r.index = indices.front();
    r.indices = indices;
    return r;
}

std::vector<MelodyNote> melodyClipboard(const Progression& p, const std::vector<std::size_t>& indices) {
    std::vector<MelodyNote> out;
    for (auto i : indices) {
        if (i < p.melody.size()) out.push_back(p.melody[i]);
    }
    return out;
}

}  // namespace chordcore
