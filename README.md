# ChordSketch

A Hookpad-inspired chord sequencer, built as a Windows VST3 plugin (C++17, JUCE, CMake). Primary
host: Ableton Live 12.4.16. "ChordSketch" is a placeholder name.

**Status: milestone 1 complete (verified in Ableton Live 12.4.16 by the user); milestone 3 (live MIDI output) implemented and unit-tested, not yet tried in Live.** MIDI export comes first; the timeline UI and live playback come later.
See [`docs/PLAN.md`](docs/PLAN.md) for the architecture, milestones and risks.

## What works today, and how it was checked

| Capability | Status | Evidence |
|---|---|---|
| Four-chord progression model (I–V–vi–IV, C major) | Done | unit tests |
| Chords → MIDI notes (triads, sevenths, inversions, sus2/sus4, major/minor keys, Roman numeral parsing) | Done | unit tests, hand-computed pitches |
| Standard MIDI File export (tempo, time signature, key signature, note lengths, tick placement) | Done | golden bytes, independent C++ parser, independent Python parser, seeded property test |
| Core builds with `-Werror` | Verified with g++ 13 and clang 18 (locally and in CI) | local build, Actions |
| Plugin sources compile against JUCE 8.0.17 headers | **Syntax-only check on Linux** | not a link, not a Windows build |
| Windows / MSVC build of the VST3 | **Built in CI** (GitHub Actions `windows-latest`, MSVC, Release, 0 warnings); all 66 tests pass on Windows | Actions run for commit `584208d`. The `.vst3` was built but never loaded in a host |
| Loads in Ableton Live | **Loads and opens** (user-reported on Live 12.4.16, the target version, commit `45d87de`). "Export MIDI..." button works (user-reported). Exported `.mid` imports with correct chords, timing, tempo and octave (user-reported) | manual checklist below |
| Live MIDI output from `processBlock`, synced to the host transport | Core scheduler **unit-tested** (81 tests incl. start, stop, locate, loop, tempo, exact sample positions, agreement with the exported file); plugin wiring **syntax-checked only** | not yet run in Live: see the milestone 3 checklist below |
| State persistence, undo/redo, timeline UI, drag-out of `.mid` | Not implemented (milestones 4, 6, 7) | |

## Layout

```
core/     chordcore: static library, no JUCE dependency (theory, model, note engine, SMF writer)
tests/    Catch2 unit tests, an independent SMF parser (tests/support), a Python cross-check
tools/    export_demo: writes demo.mid through the same pipeline as the plugin
plugin/   JUCE VST3 shell: adapts chordcore to JUCE and contains no music logic
docs/     PLAN.md
```

Design rule: `generateNotes()` is the **only** place chords become notes. File export uses it now;
transport playback will use the same output later.

## Build on Windows

Requirements: Windows 10/11, Visual Studio 2022 or newer with the "Desktop development with C++" workload
(includes CMake), Git, internet access on first configure (CMake downloads JUCE and Catch2).
Python 3 is optional (enables one extra test).

```bat
git clone <this repo> chord-sketch
cd chord-sketch
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The plugin is produced at
`build\plugin\ChordSketch_artefacts\Release\VST3\ChordSketch.vst3`.
The demo MIDI file is `build\tests\demo.mid` (written by the `export_demo_writes_mid` test), or run
`build\tools\Release\export_demo.exe my.mid`.

Useful options (add to the first `cmake` command):

| Option | Default | Meaning |
|---|---|---|
| `-DHC_BUILD_PLUGIN=OFF` | ON | skip JUCE and the plugin (core + tests only; also the way to build on Linux) |
| `-DHC_BUILD_TESTS=OFF` | ON | skip Catch2 and the tests |
| `-DHC_WARNINGS_AS_ERRORS=ON` | OFF | `-Werror` / `/WX` for chordcore |
| `-DHC_JUCE_SOURCE_DIR=<path>` | (fetch) | use an existing JUCE checkout |
| `-DHC_JUCE_TAG=<tag>` | `8.0.17-security` | JUCE version to fetch |
| `-DHC_STATIC_MSVC_RUNTIME=OFF` | ON | use the DLL runtime instead of `/MT` |

## Build core and tests on Linux/macOS (no plugin)

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DHC_BUILD_PLUGIN=OFF -DHC_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Tests

- `chordcore_tests` (Catch2): theory/pitch, Roman numeral parsing, progression time arithmetic,
  note engine (pitch, start tick, length), MIDI file bytes.
- MIDI checks use three independent routes: a hand-assembled golden byte sequence, VLQ vectors from
  the SMF specification, and a strict parser written separately from the writer
  (`tests/support/SmfParser.*`).
- `python_verifies_demo_mid` runs `export_demo`, then `tests/python/verify_midi.py` re-reads the file
  with a separate stdlib-only parser and hand-written expectations. If the third-party `mido`
  package is installed it also loads the file with it.
- The suite was mutation-checked once: five deliberately injected bugs (note-on/off order, length,
  inversion, tempo rounding, key-signature wrap) were each caught.

## Manual test checklist (Ableton Live 12.4.16, Windows) — steps 1-4 done, 5-6 not yet reported

Do the MIDI-file steps first; they need no plugin.

1. `export_demo.exe demo.mid`. Drag `demo.mid` onto an empty MIDI track in Live.
   Expect: 4 bars at 120 BPM, chords C – G – Am – F, each one bar long, notes aligned to the grid.
   Live shows MIDI note 60 as C3, so the chords sit around C2–B2.
2. Copy `ChordSketch.vst3` to `C:\Program Files\Common Files\VST3\`. In Live: Preferences → Plug-ins →
   enable "Use VST3 Plug-in System Folders", then Rescan. Check that it appears under Instruments.
3. Load it on a MIDI track. Expect the editor to open (420×180) with a summary line and an
   "Export MIDI..." button. Close and reopen the editor.
4. Click "Export MIDI...", save, then drag the saved file into Live. It should match step 1.
5. Save the Live set, reload it, delete and re-add the plugin. Confirm there are no crashes.
6. Note down anything unexpected: this is the first test in a real host.

Known Live behaviour: Live has no slot for third-party VST3 MIDI effects, so a plugin that produces
MIDI is loaded as an instrument and its MIDI output is picked up through another track's
"MIDI From" setting. Milestone 3 will need to confirm this in practice.

## Manual test checklist: milestone 3, live MIDI output (Ableton Live 12.4.16) — not yet run

The plugin makes no sound; it outputs MIDI, and Live must route that MIDI to another track.

1. Track 1: load ChordSketch (as an instrument). Track 2: load any instrument (e.g. Wavetable).
2. On track 2 set **MIDI From** to track 1 and, in the second dropdown, choose **ChordSketch**. Set Monitor to **In**.
3. Press play from bar 1. Expect C, G, Am, F, one bar each at the project tempo, then silence.
   For comparison, drop `demo.mid` on a track 3 with the same instrument: both should sound identical.
4. Stop in the middle of a chord: the sound must stop (no stuck note).
5. Start playback from bar 3 (click the arrangement ruler): Am should sound immediately.
6. Loop bars 1–4 (Ctrl+L): the progression should repeat cleanly. Loop only bars 2–3 as well.
7. Change the project tempo (e.g. 90 BPM): chord changes should still land on bar lines.
8. Play past bar 5: silence, and no stuck notes after stopping.

If track 2 never receives MIDI, check that ChordSketch appears in the second "MIDI From" dropdown at all;
that tells us whether Live exposes the plugin's MIDI output, which is the main open risk.

Known limitations: the progression is fixed (the demo); it plays against the host's bar 1 regardless of the
project's time signature; a loop wrap is noticed one audio block late, so notes ending exactly at the loop end
may release up to one block late.

## Licensing

JUCE is dual-licensed (AGPLv3 / commercial). Distributing a plugin built with JUCE requires either
releasing it under a compatible open-source licence or holding a JUCE commercial licence. Check
<https://juce.com/legal/juce-8-licence/> (and JUCE's `LICENSE.md`) before distributing binaries. This repository has no licence file yet.
