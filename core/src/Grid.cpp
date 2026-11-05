#include <chordcore/Grid.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chordcore {

Tick beatTicks(const TimeSignature& ts) {
    return ts.isValid() ? static_cast<Tick>(kTicksPerQuarter) * 4 / ts.denominator : static_cast<Tick>(kTicksPerQuarter);
}

MusicalPosition positionOf(const TimeSignature& ts, Tick tick) {
    MusicalPosition m;
    const Tick beat = beatTicks(ts);
    const Tick bar = ts.isValid() ? ts.ticksPerBar() : beat * 4;
    tick = std::max<Tick>(tick, 0);

    const Tick inBar = tick % bar;
    m.bar = static_cast<int>(tick / bar) + 1;
    m.beat = static_cast<int>(inBar / beat) + 1;
    m.ticksIntoBeat = inBar % beat;
    m.beatLength = beat;
    if (m.ticksIntoBeat == 0) m.sub = Subdivision::Beat;
    else if (beat % 4 == 0 && m.ticksIntoBeat == beat / 4) m.sub = Subdivision::E;
    else if (beat % 4 == 0 && m.ticksIntoBeat == beat / 2) m.sub = Subdivision::And;
    else if (beat % 4 == 0 && m.ticksIntoBeat == beat / 4 * 3) m.sub = Subdivision::A;
    else m.sub = Subdivision::Other;
    return m;
}

Tick tickOf(const TimeSignature& ts, int bar, int beat, Subdivision sub) {
    if (!ts.isValid()) throw std::invalid_argument("invalid time signature");
    if (bar < 1) throw std::invalid_argument("bar numbers start at 1");
    if (beat < 1 || beat > ts.numerator) throw std::invalid_argument("no such beat in this time signature");
    const Tick beatLen = beatTicks(ts);
    Tick offset = 0;
    switch (sub) {
        case Subdivision::Beat: offset = 0; break;
        case Subdivision::E: offset = beatLen / 4; break;
        case Subdivision::And: offset = beatLen / 2; break;
        case Subdivision::A: offset = beatLen / 4 * 3; break;
        case Subdivision::Other: throw std::invalid_argument("not a named subdivision");
    }
    return static_cast<Tick>(bar - 1) * ts.ticksPerBar() + static_cast<Tick>(beat - 1) * beatLen + offset;
}

std::string positionLabel(const TimeSignature& ts, Tick tick) {
    const auto m = positionOf(ts, tick);
    std::string text = "Bar " + std::to_string(m.bar) + ", beat " + std::to_string(m.beat);
    switch (m.sub) {
        case Subdivision::Beat: break;
        case Subdivision::E: text += " e"; break;
        case Subdivision::And: text += " &"; break;
        case Subdivision::A: text += " a"; break;
        case Subdivision::Other: text += " +" + std::to_string(m.ticksIntoBeat) + "t"; break;
    }
    return text;
}

std::vector<RulerMark> buildRuler(const TimeSignature& ts, Tick firstTick, Tick lastTick, double ppt, Tick gridUnit) {
    std::vector<RulerMark> marks;
    if (!ts.isValid() || !(ppt > 0.0) || lastTick < firstTick) return marks;
    firstTick = std::max<Tick>(firstTick, 0);

    const Tick beat = beatTicks(ts);
    const Tick bar = ts.ticksPerBar();

    // Distance between marks: the snap unit when it is finer than a beat (and tiles it), else the beat.
    Tick step = beat;
    if (gridUnit <= 1) step = beat / 4;                         // Snap Off: show sixteenths
    else if (gridUnit < beat) step = (beat % gridUnit == 0) ? gridUnit : beat / 4;
    if (step < beat && static_cast<double>(step) * ppt < 2.0) step = beat;  // far too small to matter

    const double barPixels = static_cast<double>(bar) * ppt;
    const double beatPixels = static_cast<double>(beat) * ppt;
    const double stepPixels = static_cast<double>(step) * ppt;

    Tick barEvery = 1;  // at low zoom only every 2nd, 4th, 8th... bar number is written
    while (static_cast<double>(barEvery) * barPixels < 26.0 && barEvery < 1024) barEvery *= 2;

    for (Tick t = ((firstTick + step - 1) / step) * step; t <= lastTick; t += step) {
        RulerMark m;
        m.tick = t;
        if (t % bar == 0) {
            m.level = MarkLevel::Bar;
            const Tick barIndex = t / bar;
            m.label = std::to_string(barIndex + 1);
            m.showLine = barPixels >= 3.0;
            m.showLabel = barIndex % barEvery == 0;
        } else if (t % beat == 0) {
            m.level = MarkLevel::Beat;
            m.label = std::to_string((t % bar) / beat + 1);
            m.showLine = beatPixels >= 5.0;
            m.showLabel = beatPixels >= 22.0;
        } else {
            m.level = MarkLevel::Subdivision;
            const Tick offset = t % beat;
            if (beat % 4 == 0) {
                if (offset == beat / 2) m.label = "&";
                else if (offset == beat / 4) m.label = "e";
                else if (offset == beat / 4 * 3) m.label = "a";
            }
            m.showLine = stepPixels >= 5.0;
            m.showLabel = !m.label.empty() && stepPixels >= 16.0;
        }
        marks.push_back(std::move(m));
    }
    return marks;
}

Tick scrollExtent(const Progression& p) {
    const Tick bar = p.timeSignature.isValid() ? p.timeSignature.ticksPerBar() : 3840;
    return std::max(p.lengthTicks(), bar * 4) + bar;
}

TimelineView fitView(const Progression& p, double widthPixels) {
    TimelineView v;
    v.originTick = 0;
    v.pixelsPerTick = fitPixelsPerTick(p, widthPixels);
    return v;
}

TimelineView clampView(TimelineView v, Tick extent, double width) {
    width = std::max(width, 1.0);
    const double minPpt = extent > 0 ? std::min(width / static_cast<double>(extent), kMaxPixelsPerTick) : 1e-4;
    v.pixelsPerTick = std::clamp(std::isfinite(v.pixelsPerTick) ? v.pixelsPerTick : minPpt, minPpt, kMaxPixelsPerTick);
    const auto visible = static_cast<Tick>(std::ceil(width / v.pixelsPerTick));
    v.originTick = std::clamp<Tick>(v.originTick, 0, std::max<Tick>(0, extent - visible));
    return v;
}

TimelineView zoomView(const TimelineView& v, double anchorX, double factor, Tick extent, double width) {
    if (!(factor > 0.0) || !std::isfinite(factor) || !(v.pixelsPerTick > 0.0)) return clampView(v, extent, width);
    const double anchorTick = static_cast<double>(v.originTick) + anchorX / v.pixelsPerTick;
    TimelineView next = v;
    next.pixelsPerTick = v.pixelsPerTick * factor;
    next = clampView(next, extent, width);  // limits the zoom before the anchor is re-applied
    next.originTick = static_cast<Tick>(std::llround(anchorTick - anchorX / next.pixelsPerTick));
    return clampView(next, extent, width);
}

TimelineView scrollView(const TimelineView& v, double deltaPixels, Tick extent, double width) {
    TimelineView next = v;
    if (v.pixelsPerTick > 0.0 && std::isfinite(deltaPixels)) next.originTick += static_cast<Tick>(std::llround(deltaPixels / v.pixelsPerTick));
    return clampView(next, extent, width);
}

}  // namespace chordcore
