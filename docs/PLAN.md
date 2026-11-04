# ChordSketch — Technical Implementation Plan

A Hookpad-inspired VST3 chord sequencer (C++17, JUCE, CMake). Primary host: Ableton Live 12.4.16 on Windows.
"ChordSketch" is a placeholder name; it deliberately avoids the Hookpad trademark.

## 1. Environment findings (where this plan was written)

| Item | Finding |
|---|---|
| Authoring machine | Linux x86_64, g++ 13.3, clang 18.1, CMake 3.28, Ninja, git, Python 3.13 |
| GitHub access | Works (JUCE and Catch2 can be fetched with `FetchContent`) |
| PyPI / apt mirrors | **Unreachable** (so `mido` could not be installed, and JUCE's Linux dev packages X11/Xrandr, ALSA, cURL could not be installed) |
| Windows / MSVC | **Not available** here |
| Ableton Live | **Not available** here |

Consequence: the JUCE-independent core library and its tests are built and run on Linux here. A full
JUCE build is impossible on this machine even for Linux (JUCE's own `juceaide` tool needs
`X11/extensions/Xrandr.h`), so the plugin could only be syntax-checked against the real JUCE headers.
The Windows build and the Ableton load test are covered by a manual checklist (README) and a GitHub
Actions Windows job that has not yet been run.

## 2. Architecture

```
chord-sketch/
  core/      chordcore — static library, NO JUCE dependency, C++17
    Types.h          Tick (int64), kTicksPerQuarter = 960, TimeSignature
    Theory.h         Key, Mode, ChordSpec, voiceChord()      (music theory)
    RomanNumeral.h   parseRomanNumeral()                      (text -> ChordSpec)
    Progression.h    Progression / ChordBlock value types     (the model)
    NoteEngine.h     generateNotes()                          (THE one note generator)
    MidiFile.h       buildMidiFile(), exportProgressionToMidi()
  tests/     Catch2 unit tests + an independent SMF byte parser (tests/support)
             + tests/python/verify_midi.py (cross-check with the `mido` library)
  tools/     export_demo — writes demo.mid from the C++ pipeline (no plugin needed)
  plugin/    JUCE VST3 shell; links chordcore; contains no music logic
```

Key rules:

1. **One engine.** `generateNotes(Progression, GenerationOptions) -> vector<NoteEvent>` is the only
   place chords become notes. File export consumes its output; the future real-time playback
   scheduler will consume the same output (precomputed off the audio thread).
2. **Integer ticks, 960 per quarter.** No floating-point beat arithmetic in the note path. 960 is
   also the SMF division, so export is exact.
3. **Model independent of UI.** `Progression` is a plain value type (copyable), which also makes
   undo/redo straightforward (command objects or snapshots).
4. **SMF written by us, not by `juce::MidiFile`**, so the core has no JUCE dependency, bytes are
   fully deterministic, and tests can assert on raw bytes. Format 0 (single track) by default:
   tempo, time signature, key signature and notes are all in one track, which avoids a
   conductor-only track that a host might import as an empty clip.
5. **Note-off sorts before note-on at the same tick**, so adjoining chords re-trigger cleanly.

## 3. Milestones

| # | Scope | Gate |
|---|---|---|
| **M1 (this)** | Core types, theory (triads, sevenths, inversions, major/minor keys), basic Roman numeral parser, sus2/sus4 (added after M1 sign-off request), 4-chord model, note engine, SMF export, CLI demo, unit tests, minimal JUCE shell with "Export MIDI…" button | Core tests pass; `.mid` validated independently (byte parser + mido); plugin compiles on Windows; loads in Live |
| M2 | Range/selection export (clip + shift ticks), optional Format 1, tempo map | Tests for clipped notes and range offsets |
| M3 (**implemented**) | Transport-synchronised MIDI output in `processBlock` from the same `NoteEvent` list (sample-accurate offsets, loop/jump/stop handling, stuck-note protection) | Scheduler unit tests with synthetic playheads; manual Live test |
| M4 (**implemented**, awaiting Live check) | State persistence (versioned JSON), undo/redo | Round-trip, golden-format, hostile-input and undo property tests; manual save/reload in Live |
| M5 | Extended harmony: 9ths, alterations, secondary dominants, minor-key convention options | Theory tests per chord type |
| M6 | Timeline UI: draggable/resizable chord blocks, snapping, Roman numeral entry | UI is a thin layer over the model |
| M7 | Native file drag-out from the plugin (`performExternalDragDropOfFiles`), Ableton verification | Manual test in Live 12.4.16 |

Advanced features (M2+) are intentionally not started until M1's export has been independently checked.

## 4. Verification matrix (M1)

| Claim | How verified | Where |
|---|---|---|
| Chord pitches, inversions, sevenths | Catch2 tests with hand-computed expectations | **Passed**, Linux (g++ 13 Debug, clang 18 Release) |
| Note start/length ticks | Catch2 tests | **Passed**, Linux |
| SMF structure, tempo/time/key meta, delta times, note on/off order | Hand-assembled golden bytes, spec VLQ vectors, independent strict C++ parser, seeded property test (300 random progressions) | **Passed**, Linux |
| Tests can actually fail | Five injected bugs (on/off order, length, inversion, tempo rounding, key-sig wrap) | **All five caught** |
| `.mid` re-read by a second implementation | `tests/python/verify_midi.py` (stdlib parser written for this project, hand-written expectations) | **Passed**, Linux |
| `.mid` read by a genuinely third-party library | `mido` (optional branch in the same script) | **Not run**: `mido` could not be installed here |
| Plugin source compiles with JUCE | `g++ -fsyntax-only` of both plugin `.cpp` files against JUCE 8.0.17 headers, plus a negative control | **Passed (syntax only)**. Not linked, not run, not a Windows result |
| Plugin CMake target (`juce_add_plugin`) configures | Arguments cross-checked by reading JUCE's CMake API docs and example only | **Not executed** (configure stops in `juceaide`, missing X11 headers). One real defect found on the way: `project()` lacked the `C` language JUCE requires (fixed) |
| Scheduler: exact sample positions, stop/locate/loop, mid-chord start, tempo, stuck-note protection, agreement with exported file | 15 scheduler tests incl. a 200-iteration random-transport property test | **Passed**, Linux (g++) |
| Scheduler tests can actually fail | Six injected bugs (off/on order, no release on stop, no jump detection, truncated rounding, ignored tempo, no mid-chord start) | **All six caught** |
| Plugin `processBlock` wiring | `-fsyntax-only` against JUCE 8.0.17 headers; CI build; manual test in Live | **Passed** in CI (commit `6c063d0`); **works in Live 12.4.16** (user-reported). Confirms an instrument-type VST3 can send MIDI to another track via "MIDI From" |
| Compiles on Windows / MSVC | `.github/workflows/build.yml` on GitHub Actions `windows-latest` | **Passed** (commit `584208d`): plugin builds, 66/66 tests pass, 0 MSVC warnings. First run failed only because the workflow hardcoded the VS 2022 generator (fixed) |
| Loads in Ableton Live | Manual checklist in README | **Loads** on Live 12.4.16 (user-reported, commit `45d87de`) after fixing the missing MIDI input bus. Target version is 12.4.16 |
| `.mid` imports correctly into Live | Manual: drag `demo.mid` into a MIDI track | **Not yet run** |

## 4b. Milestone 3 design notes

- `PlaybackScheduler` (core, JUCE-free) consumes the same `NoteEvent` list as the file export. The plugin
  only reads the host playhead (running, ppq, bpm) and copies events into the `MidiBuffer`.
- Each sample owns the ticks within half a sample either side; consecutive blocks chain their tick windows
  exactly, so each tick belongs to exactly one block (found by testing: an earlier version played a note one
  sample early when floating-point noise put a bar-line tick in the previous block).
- Locate, loop wrap and start are detected as position discontinuities; sounding notes are released first and
  any note sounding at the new position starts immediately.
- No allocation in `process()`; `setNotes()` pre-sizes everything. Editing needs a safe hand-over
  of a new note list to the audio thread (done in M4, see 4c).

## 4c. Milestone 4 design notes

- **Format.** One JSON document (`format: "chordsketch-state"`, `version: 1`), hand-written reader/writer in
  core (no dependency, JUCE-free, same code path in tests). Enums are written as words, tempo with
  shortest-round-trip `std::to_chars`, so save -> load is exact and files are diffable. A golden test pins the
  bytes: changing the format means bumping `kStateVersion` and writing a migration. Unknown fields are ignored
  (forward compatibility), `suspension` is optional (written by this version, absent in hypothetical older
  files), versions above 1 are refused with a message, and every loaded progression passes `validate()`.
- **Undo/redo.** `Document` keeps value snapshots (a progression is small). Edits are applied to a copy,
  validated, then committed, so a rejected edit changes nothing. Consecutive edits with the same coalesce key
  (tempo slider drag) form one undo step. History is bounded (200) and not persisted.
- **Threads.** The audio thread never sees the `Document`. After each change the editing thread regenerates the
  note list (still the single `generateNotes`) and swaps it into the scheduler under a spin lock that the audio
  thread only *tries* to take: during the swap one block is skipped, nothing blocks. Swapping releases notes that
  were sounding (tested). Rebuild + install happen inside the document lock so note lists install in edit order.
- **Known trade-offs.** `setNotes()` allocates while the audio thread is locked out (microseconds, only on edit).
  A skipped block looks like a position jump to the scheduler, which handles it as a locate (release + restart).

## 5. Risks and open decisions

- **Plugin type in Ableton.** Live has no VST3 "MIDI effect" slot for third-party plugins. The
  documented route for plugins that generate MIDI is to load them as an instrument and route their
  output via another track's "MIDI From" ([Ableton help](https://help.ableton.com/hc/en-us/articles/209070189-Accessing-the-MIDI-output-of-a-VST-plug-in);
  that article does not mention VST3 specifically). M1 declares the plugin as `IS_SYNTH` +
  `NEEDS_MIDI_OUTPUT`; this must be confirmed in Live.
  **Confirmed by test (Live 12.4.16, 2026-11-04):** Live rejects an instrument without a MIDI input bus
  ("plugin has instrument category, but no valid event input bus"), so `NEEDS_MIDI_INPUT TRUE` /
  `acceptsMidi() == true` are required. pluginval does not check this rule.
- **Minor-key numeral convention.** M1 interprets numerals against the key's own scale (natural
  minor for minor keys, so `III`, `VI`, `VII` are the scale's own degrees; `bVII` lowers further).
  Hookpad-style tools may use another convention. Decide before M5.
- **Middle C naming.** MIDI note 60 is displayed as C3 in Ableton (C4 in many other DAWs). Default
  voicing places chord roots in 48–59, i.e. C2–B2 in Live's naming; voicing range is configurable.
- **Drag-and-drop (M7)** depends on host window handling; JUCE supports external file drags on
  Windows but Live's behaviour must be tested.
- **JUCE version.** Pinned to `8.0.17-security` via a CMake cache variable (`HC_JUCE_TAG`); JUCE 9.x
  tags exist but were not evaluated. In this version `AudioProcessor` lives in the
  `juce_audio_processors_headless` module (pulled in automatically by `juce_audio_processors`).
- **JUCE licence.** AGPLv3 or the commercial JUCE licence; decide before distributing binaries.
- **MIDI time-signature meta.** The writer always emits "24 clocks per click, 8 32nd-notes per
  quarter". Hosts ignore these fields for import, but a notation-oriented consumer might care.
  Revisit when Format 1 / tempo maps are added (M2).
