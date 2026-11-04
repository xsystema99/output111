#!/usr/bin/env python3
"""Independent check of a .mid file written by chordcore. Standard library only.

This is a second, separate implementation of an SMF reader (different language from the C++ test
parser), with its own hand-written expectations for the demo progression. It is NOT a third-party
tool: it was written for this project. If the `mido` package is installed, the file is additionally
loaded with it as a genuinely third-party cross-check.

usage:
    verify_midi.py FILE.mid            structural checks + note listing
    verify_midi.py --demo FILE.mid     also check the milestone progression (C major, I-V-vi-IV)
"""
import struct
import sys


def read_vlq(data, pos):
    value = 0
    for _ in range(4):
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, pos
    raise ValueError("VLQ longer than 4 bytes")


def parse(data):
    if data[0:4] != b"MThd":
        raise ValueError("missing MThd")
    length, fmt, ntrks, division = struct.unpack(">IHHH", data[4:14])
    if length != 6:
        raise ValueError("MThd length != 6")
    if division & 0x8000:
        raise ValueError("SMPTE division")
    pos = 14
    tracks = []
    for _ in range(ntrks):
        if data[pos:pos + 4] != b"MTrk":
            raise ValueError("missing MTrk at byte %d" % pos)
        (tlen,) = struct.unpack(">I", data[pos + 4:pos + 8])
        pos += 8
        end = pos + tlen
        if end > len(data):
            raise ValueError("MTrk length past end of file")
        events, tick, status = [], 0, None
        saw_end = False
        while pos < end:
            if saw_end:
                raise ValueError("data after End-of-Track")
            delta, pos = read_vlq(data, pos)
            tick += delta
            b = data[pos]
            if b == 0xFF:
                mtype = data[pos + 1]
                n, pos = read_vlq(data, pos + 2)
                payload = data[pos:pos + n]
                pos += n
                events.append((tick, "meta", mtype, bytes(payload)))
                saw_end = saw_end or mtype == 0x2F
                status = None
            else:
                if b & 0x80:
                    status = b
                    pos += 1
                elif status is None:
                    raise ValueError("running status without prior status")
                else:
                    raise ValueError("running status used (chordcore should not emit it)")
                count = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
                events.append((tick, "voice", status, bytes(data[pos:pos + count])))
                pos += count
        if pos != end:
            raise ValueError("track length mismatch")
        if not saw_end:
            raise ValueError("track without End-of-Track")
        tracks.append(events)
    if pos != len(data):
        raise ValueError("trailing bytes after last track")
    return fmt, division, tracks


def notes(events):
    open_notes, out = {}, []
    for tick, kind, status, payload in events:
        if kind != "voice":
            continue
        typ, ch = status & 0xF0, status & 0x0F
        if typ == 0x90 and payload[1] > 0:
            open_notes.setdefault((ch, payload[0]), []).append((tick, payload[1]))
        elif typ == 0x80 or (typ == 0x90 and payload[1] == 0):
            start, vel = open_notes[(ch, payload[0])].pop(0)
            out.append((payload[0], start, tick - start, vel, ch))
    if any(v for v in open_notes.values()):
        raise ValueError("stuck note(s)")
    return sorted(out, key=lambda n: (n[1], n[0]))


# Hand-written expectations for the milestone progression: C major, I - V - vi - IV, 4/4, 120 BPM,
# one bar (4 * 960 = 3840 ticks) each, velocity 96, channel 1 (index 0).
DEMO_NOTES = [
    (48, 0, 3840, 96, 0), (52, 0, 3840, 96, 0), (55, 0, 3840, 96, 0),                # C  E  G
    (55, 3840, 3840, 96, 0), (59, 3840, 3840, 96, 0), (62, 3840, 3840, 96, 0),       # G  B  D
    (57, 7680, 3840, 96, 0), (60, 7680, 3840, 96, 0), (64, 7680, 3840, 96, 0),       # A  C  E
    (53, 11520, 3840, 96, 0), (57, 11520, 3840, 96, 0), (60, 11520, 3840, 96, 0),    # F  A  C
]


def check_demo(fmt, division, tracks):
    assert fmt == 0, "expected format 0, got %d" % fmt
    assert division == 960, "expected 960 PPQ, got %d" % division
    assert len(tracks) == 1
    ev = tracks[0]
    metas = {}
    for tick, kind, mtype, payload in ev:
        if kind == "meta":
            metas.setdefault(mtype, []).append((tick, payload))
    assert metas[0x51] == [(0, bytes([0x07, 0xA1, 0x20]))], "tempo should be 500000 us/quarter at tick 0"
    assert metas[0x58] == [(0, bytes([4, 2, 24, 8]))], "time signature should be 4/4 at tick 0"
    assert metas[0x59] == [(0, bytes([0, 0]))], "key signature should be C major at tick 0"
    assert metas[0x2F] == [(15360, b"")], "end of track should be at tick 15360 (4 bars)"
    got = notes(ev)
    assert got == DEMO_NOTES, "notes differ from expected:\n%s" % got


def main(argv):
    demo = "--demo" in argv
    paths = [a for a in argv[1:] if not a.startswith("--")]
    if len(paths) != 1:
        print(__doc__)
        return 2
    with open(paths[0], "rb") as f:
        data = f.read()
    fmt, division, tracks = parse(data)
    ns = notes(tracks[0])
    print("%s: %d bytes, format %d, %d PPQ, %d track(s), %d notes" % (paths[0], len(data), fmt, division, len(tracks), len(ns)))
    if demo:
        check_demo(fmt, division, tracks)
        print("demo progression: OK")
    try:
        import mido  # optional third-party cross-check
        m = mido.MidiFile(paths[0])
        print("mido loaded the file: type %d, ticks_per_beat %d, %d track(s)" % (m.type, m.ticks_per_beat, len(m.tracks)))
    except ImportError:
        print("mido not installed: third-party cross-check skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
