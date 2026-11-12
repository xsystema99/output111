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
| M4 (**implemented**, Live check passed per user report) | State persistence (versioned JSON), undo/redo | Round-trip, golden-format, hostile-input and undo property tests; manual save/reload in Live |
| M5 | Extended harmony: 9ths, alterations, secondary dominants, minor-key convention options | Theory tests per chord type |
| M6 (**implemented**, Live check passed per user report) | Timeline UI after the user's mockup: draggable/resizable chord cards, snapping, palette, chord types | Editing rules in core with tests; UI is a thin layer; manual check in Live |
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

## 4d. Milestone 6 design notes

- **Where the rules live.** `Timeline.h` (snap grid, move/resize/add/split/duplicate/delete, pixel<->tick mapping,
  hit testing) and `ChordEdit.h` (diatonic quality/seventh per degree, palette/type-button rules, major<->minor
  switching, chord names) are JUCE-free and unit/property/mutation tested. `TimelineComponent` only paints and
  forwards mouse events.
- **Single lane, clamped drags.** Blocks never overlap; a dragged block stops at its neighbour instead of
  jumping over it, which keeps every intermediate state valid. Reordering chords is therefore done by delete/add or
  paste for now.
- **Drags are previewed locally** and committed as one `Document` edit on mouse-up: one undo step, and playback is
  not rebuilt on every mouse move.
- **Changing scale** re-spells chords that are diatonic to the old scale (including their native 7th) and leaves
  borrowed/altered chords alone (a deliberate V in minor stays V). Suspensions that would land on a diminished chord
  are dropped.
- **Spelling.** Flats vs sharps follow the key signature (relative major); an explicitly flatted chord (bVII) is
  always spelled with flats. A test found this the first time: bVII in C was shown as A#.
- **Mockup deviations.** Export, Undo/Redo, tempo and snap are added in a tools row, since export is a core
  requirement; the Preview button is not built (it needs on-demand MIDI audition in the audio thread) and "9th" is
  disabled until M5; "Copy progression" copies the Roman-numeral text, and Paste reads it back.
- **Not verified.** Visual appearance and mouse behaviour have not been seen: JUCE cannot be built or run in the
  authoring environment; CI compiles it and the user tests it in Live.

## 4e. Preview design notes

- `PreviewTransport` (core) is only a clock: it produces the `TransportBlock` for the same `PlaybackScheduler` the host
  transport feeds. So preview timing, note release and edit-while-playing behaviour are the code already tested
  for M3/M4, and the notes are the ones the exported file contains.
- The host always wins: a request is ignored while the host plays and a running preview is cancelled when it starts
  (the scheduler then sees an ordinary locate). The preview ends itself at the end of the last chord with one
  "stopped" block so every note is released.
- Audio thread: two atomics (requested, running) plus the progression end in quarter notes; the clock lives under
  the scheduler's try-lock. The playhead line shows the preview position while previewing.
- **Open risk:** a host may stop calling `processBlock` for an instrument that receives no MIDI while its transport is
  stopped. Unknown for Live 12.4.16 until tested.
- Not done: auditioning a single chord when it is clicked in the palette (would reuse the same clock with a short range).

## 4f. Length and octave options

- **Length** (`Progression::lengthBars`, 0 = Auto): minimum clip/project length in bars. `lengthTicks()` is the longer of
  the chords rounded up to whole bars and `lengthBars` bars, and that one value drives both the exported clip's
  end-of-track and the timeline's width, so they cannot disagree. Auto keeps the previous behaviour exactly (all
  existing tests and golden bytes unchanged). Chords longer than the setting win.
- **Octaves.** `Progression::octave` shifts the whole project and `ChordBlock::octave` shifts one chord, each -2..+2,
  applied in `generateNotes` by moving the voicing base by 12 semitones per octave, so playback, Preview and MIDI export
  stay identical. The +-2 + +-2 limit was chosen because it provably keeps every voicing inside MIDI 0..127
  (checked exhaustively in a test); a wider range would need clamping.
- **Saved state.** Both options (and the per-chord octave) are written only when non-zero, so a project that does not use
  them is byte-identical to a version-1 file from before; the version number is unchanged. An older build opening a
  file that does use them ignores the fields (loses the setting, still opens). If that ever matters, bump
  `kStateVersion` instead.
- **Not carried by the chord text** (Copy/Paste): octaves are not part of the Roman-numeral line.

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

## 4g. Precision chord grid (replaces the card timeline)

- Time model unchanged: every chord has an independent `startTick` / `durationTicks` at 960 PPQ, so no migration was needed. Pixels exist only in `TimelineView` (origin + zoom) and are converted to ticks on each event; nothing derived from pixels is stored.
- `Grid.h` (JUCE-free, tested): `tickOf`/`positionOf`/`positionLabel` (beat, e, &, a), `buildRuler` (bar/beat/subdivision hierarchy, labels thin out with zoom, positions never depend on zoom), `zoomView`/`scrollView`/`clampView`/`fitView`.
- Snap (`SnapMode`): 1 bar, 1/2, 1/4, 1/8 (default), 1/16, 1/32, 1/8T (320 ticks), 1/16T (160 ticks), Off. Changing snap only changes the unit used by later edits; it never moves a chord. Insert length (default 1/4) is independent of snap.
- Insertion snaps to the start of the grid cell that was clicked. Group operations `moveBlocks` / `duplicateBlocks` / `deleteBlocks` keep relative timing, durations, chords and octaves.
- Overlap policy (single lane): nothing is ever overwritten or silently dropped. Move and resize clamp at the neighbouring chord; a group move also stops at unselected chords; inserting inside an existing chord is refused with a message; an inserted chord is shortened to fit the gap before the next one; duplicating without room fails and changes nothing. Overlapping layers can be added later because only the lane rules (Timeline.cpp) would change.
- One engine: playback, Preview and export all consume `generateNotes`, so a chord at tick 1440 lasting a quarter has note-ons at 1440 and note-offs at 2400 everywhere (tests C, G).
- GUI (`GridComponent`) previews drags on a local copy and commits one `editProgression` on mouse-up (one undo step). The audio thread is never involved.

## 4h. Combined build: text, range export, extensions, audition, resize

- Copy/Paste text gains `@+N` (chord octave); inversions and 9ths were already expressible. `formatProgressionText` / `parseProgressionText` round-trip chords, inversions, 9ths, octaves, lengths and gaps.
- Bar-range export (`exportProgressionRangeToMidi`) calls the single note generator, then clips and shifts the notes; nothing is re-timed independently.
- Extensions: `Ninth {Minor, Major, Sharp}`, `Eleventh {Perfect, Sharp}`, `Thirteenth {Minor, Major}` on a minor/major seventh. A natural 11 and a natural 13 do not combine (a "13" leaves out the 11); 11th/13th chords are root position only; 9th chords invert up to the 2nd. The symbol text (`extensionSymbol`) is shared by chord names and Roman numerals, so parse(format(x)) is exact. `Progression::validate` rejects any chord that would leave MIDI 0..127, so a bad edit is refused, never clipped silently.
- Audition: `AuditionPlayer` (core, tested) emits the same events as the scheduler; the processor feeds it under its own spin lock, only when the host is stopped and Preview is idle, and every note-on gets one note-off.
- Resize: the editor is resizable (minimum 640 x 716); extra height goes to the grid, extra width to the grid, sliders and palette.



## 4i. Melody lane, harmony colours and compatibility (Build A)

- `Harmony.h`: one resolver (key + colour mapping + palette) turns a pitch into a note colour/label (identity) and, given the active chord's pitch-class mask, into a compatibility class (chord tone > scale tone > chromatic) and a background colour. `buildCompatibility()` produces one region per chord block with its exact tick boundaries. Note colour never depends on the chord; background never depends on the colour mapping except through the tint colour.
- `Melody.h` / `MelodyEdit.h`: `MelodyNote` {pitch, start, duration, velocity, locked}, `MelodySettings` (colour mapping, palette, label mode, channels, mutes) stored in `Progression`; sorted by (start, pitch); same-pitch notes never overlap. All edit operations are core functions (add, move time+pitch, resize, delete, duplicate, paste, velocity, lock) returning new indices, undone through `Document`.
- Persistence: `melody` and `melodySettings` are optional JSON fields written only when used, so old projects load unchanged and unchanged projects serialise byte-identically (state version stays 1).
- Playback: `generateProjectNotes()` = chords (chord channel) + melody (melody channel) minus muted parts; the scheduler is fed from it. Export of the melody and the three-file export are Build B.
- UI: `MelodyRollComponent` draws with the chord grid's view (origin, zoom, snap, bar/beat lines), precomputes backgrounds when the model changes, paints only rectangles.
- Decisions: lock protects a note from the generator only (hand edits still work). Chord Density and Chromatic Dice do not exist in this codebase; spec test I is expressed as replacing a chord.

## 4j. Melody export and mutes (Build B)

- `exportProject()` is the one export path for Chords.mid / Melody.mid / Combined.mid (optional bar range, optional melody-on-channel-1). It builds from `generateNotes()` (chord channel) and `generateMelodyNotes()` (melody channel), the same events `generateProjectNotes()` gives playback; a test pairs the Combined file's notes against the playback events.
- Combined is SMF Format 1: a conductor track (name, time signature, tempo, key) and tracks "Chords" and "Melody" without their own tempo events; all three share one end-of-track.
- Mutes are playback-only (they remove a part from `generateProjectNotes()`); exports always contain both parts.
- Editor: export dialog (part, bar range, melody channel), drag-part box, mute toggles (undoable, saved).

## 4k. Melody generator (Build C)

- `MelodyGenerator.h/.cpp`: pure and deterministic (own SplitMix64 generator seeded from seed and range start; no `<random>` distributions). Onsets per bar on an eighth grid from density and rhythm style with motif repetition; pitches by a score over the register (chord tones, scale tones, tension-gated chromatics, half-step clash, contour target, interval size, common tones, phrase-end root, repetition, seeded jitter). Locked notes and notes outside the range are obstacles and melodic context.
- `regenerateMelody()` is the only mutation: removes unlocked notes starting in the range, adds the new ones, records the chords it generated against (`melodyBasis`: start, length, chord hash). `affectedRanges()` / `regenerateAffectedMelody()` compare current chords with that basis (changed, added or removed chords). Chord edits never touch the melody.
- Tests: determinism, golden output (pinned notes for seed 1, run on Windows in CI too), register, density ordering, contour over 20 seeds, zero tension, strong-beat chord tones, phrase-end roots, locks, region isolation, affected regeneration, undo/redo, save/reload (spec tests H-K).
- Editor: `GeneratorPanel` in a call-out box; settings are undoable project edits; actions run on the message thread as single edits.
- Open: Chord Density and Chromatic Dice are not in this codebase, so test I was written against chord replacement.

## 4l. Chord Density 2.0 (CHORD / DEFINE phase, step 1: core engine)
- `Voicing.h/.cpp` (JUCE-free): `VoicingSettings` {density 0 = Auto/Original or 1..12, register Low/Mid/High/Full/Custom, distribution Automatic/Close/Open/Spread, preserveBass, voiceLeading}, global in `Progression::voicing`, optional per-chord override in `ChordBlock::voicing`. Saved as optional `"voicing"` objects with only non-default fields, so projects that do not use it are byte-identical to before.
- `voiceChordWithSettings()` is pure and deterministic and never runs on the audio thread. Auto returns exactly `voiceChord()`. Reduction keeps tones by a context-sensitive priority (explicit bass, root, third/sus, seventh, altered fifth and alterations, 13th/9th/11th, plain fifth); expansion adds octave doublings of chord tones only (root, fifth, third, ...). Register presets follow the chord octave shift; Custom is absolute. If the register cannot hold the request, `actualVoices < targetVoices` and `note` says why.
- Each voice carries `{pitch, role (Bass/Lower/Middle/Upper/Extension), voiceId, interval, original, doubling}`. `voiceId = interval*8 + copy`, stable across density changes, not a vector index (reserved for the future CHORD/GATE stage; gating is not implemented).
- `voiceProgression()` voices every block in time order (voice leading chains through the previous chord: movement, leaps, shared tones, register and bass continuity, crossing; integer cost, bounded candidates). `generateNotes()` is built on it, so playback, Preview, audition and every export use the same pitches. `GenerationOptions::originalVoicing` forces Auto for a future "Original Chords.mid".
- Tests (`test_voicing.cpp`): Auto restores original, triad to 6, 1..12 across distributions/registers, C7 at 3 = C-E-Bb, Fm9 extensions, register bounds, narrow-register report, preserve bass, ids/roles, voice-leading determinism, timing/melody untouched, per-chord override, one engine for playback and export, persistence and legacy round trip. Mutation-checked (priority table, preserve bass, doubling order).
- Not yet: Inspector UI, Chromatic Dice, slash bass, CHORD/GATE.

## 4m. Transport fix: pattern loops with the host (user-reported)
- Bug: note ticks were compared directly with the host position, so a 4-bar pattern only sounded at bars 1-4; starting at bar 65 was silent.
- Fix: `PlaybackScheduler::setLoopLength()` folds the host position into `[0, Progression::lengthTicks())`. A block crossing the pattern end is split, so the wrap is sample-accurate at any block size. Negative host positions (count-in) are not folded. The processor sets the loop length on every playback rebuild and the playhead shows the position inside the pattern. Tempo comes from the host on every block (fallback BPM only without a host position).
- Also fixed: a locate exactly on a chord boundary no longer fires the previous chord for one sample.
- Tests: bar-65 start, wrap at block sizes 64/480/1000/4096 against first-principles sample positions, host tempo change, stop releases notes.

## 4n. Chromatic Dice 2.0 (CHORD / DEFINE phase, step 2: core engine)
- `DiceSettings.h` (stored in `Progression::dice`, optional `"dice"` field with only non-default values) and `ChromaticDice.h/.cpp`. Candidates are built by stacking thirds on every degree of the parallel modes of the PROJECT tonic (Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, Locrian, Harmonic minor, ascending Melodic minor): triads, sevenths and (Adventurous+) ninths; each is converted to a `ChordSpec` and kept only if the chord model reproduces exactly the stacked intervals. Identical sounds from several modes are merged with all their source modes. Roman numerals are chromatic relative to the tonic (`bVImaj7`, `v`, `#ivo`; the app writes diminished as "o").
- Conservative: Ionian/Dorian/Mixolydian/Aeolian, triads + sevenths. Adventurous: all nine modes + ninths. Unrestricted: also any root x a catalogue of qualities (chromatic, no source mode). A Source Mode filter overrides the tier's mode list; Unrestricted chromatic chords need no filter.
- Preserve Root / Root Lock keep the root pitch class; Replace Harmony allows any root. Avoid Repeats skips the neighbours' sounds and recent rolls. Empty pool: chord unchanged, message returned.
- Selection: integer score (shared tones, root motion on the circle of fifths, voice movement, fit with neighbours, tension) then a weighted pick with integer weights (Conservative cubes the score gap, Adventurous squares it, Unrestricted is nearly flat), SplitMix64 seeded from seed, rollIndex and the block position; no floating point, so results match across compilers.
- Normalisation policy: position, duration, octave, density settings and melody are untouched; the new chord's own extensions replace the old; inversion is kept if supported, otherwise reduced (with an explanation). No slash bass exists in the model yet.
- Undo/redo: the roll returns a concrete chord that the caller stores in one `Document::edit` (with `dice.rollIndex` advanced), so undo restores the old chord and redo restores exactly the rolled chord. The melody is never regenerated; `affectedRanges()` flags it.
- Mutation-checked: root filter, source merging, weighted pick, inversion clamp, repeat avoidance.

## 4o. Harmonic Inspector, export voicing, CHORD/GATE slot (CHORD / DEFINE phase, steps 3-4)
- `plugin/Source/HarmonicInspector.h/.cpp`: one panel docked to the right of the timeline (header "Inspector" button collapses it and shrinks the window; the panel scrolls when the window is short). It holds no chord state: it is given the model plus the timeline selection and edits only through `applyToSelected` (every selected chord, one undo step) and `editProgression` (global/dice settings).
  - HARMONIC DEFINITION: chord name, Roman numeral, root, quality, extensions, alterations/sus, inversion and bass, project key, source parallel modes, interpretation (diatonic / borrowed / chromatic); multi-selection shows shared values or "Mixed"; empty state text. Editors: root, quality, seventh (core helpers `withRootPitchClass`, `withQuality`, `withSeventhKind`), 9/11/13, b9/#9/#11/b13, sus2/sus4, inversion. Chromatic Dice: Dice Roll, Reroll, Undo, Preserve Root / Replace Harmony, Complexity, Source Mode, Avoid repeats, Root lock, seed; single chord only; feedback line; "Regenerate Affected Melody" (enabled only when chords changed; locked notes stay).
  - VOICE ALLOCATION: scope Global / Selected chords (with "Use global"), density Auto/Original or 1-12, register, layout, Preserve Bass, Voice Leading, custom register sliders, target vs actual voices (and why if fewer), pitch preview keyboard coloured by the central HarmonyResolver (B = bass, dot = octave doubling, ring = original tone, hollow ring = no longer sounding, dimmed = outside register).
  - PERFORMANCE: a labelled placeholder with no controls.
- Window: default width 640 + dock; the header title reads CHORD / DEFINE (display text only; VST3 identity is untouched).
- Export: `ProjectExportOptions::originalVoicing` ("Original Chords.mid"); the default export is the voiced one, produced by the same `generateNotes()` as playback. A "Gated Chords.mid" will come with CHORD/GATE.
- `PerformanceStage` (NoteEngine.h): the slot between Voice Allocation and MIDI output, with `EventOrigin{block, voiceId, role}` per event. Nothing implements it in the product; a test stand-in proves it addresses voices by identity and that a null stage changes nothing.
- Not done: slash bass (needs the chord model, naming and Roman-numeral parser changed together), CHORD/GATE itself, persisting the dock open/closed state, multi-select batch Dice.

## 4p. Configurable MIDI filenames: Absolute / Roman / Both
- One generator, `generateMidiFilename()` (`MidiFilename.h/.cpp`), used by the live preview, "Export MIDI..." and the drag handle. `Progression::exportNaming` (`MidiFilenameFormat`) is saved with the project as optional `"exportNaming"` (absent = Absolute, so older projects load and re-save byte-identically).
- Names come from the authoritative model: `chordName()` for absolute (spelling untouched: Db stays Db) and the new `chromaticRomanNumeral(key, chord)` for Roman. The numeral reads the root against the major scale on the project tonic in either mode (F minor: Fm9 = i9, Db = bVImaj7, Ab = bIIImaj9, Eb7 = bVII7), then reuses `formatRomanNumeral()` for case, quality, extensions, alterations, diminished/half-diminished and inversion figures. The existing timeline labels keep the diatonic form; the Inspector, Chromatic Dice and filenames use the chromatic one. A chord the Roman grammar cannot express shows its absolute name in Roman mode.
- Order and region: chords sorted by start tick (stable for ties), repeats kept, selected-bar exports include the chords that overlap the bars. No beats or durations in names. Display uses the flat/sharp/degree signs and an en dash; `ascii=true` is the documented fallback (b, #, -, m7b5), used automatically if the file system refuses the Unicode name when creating the drag file.
- Suffixes: `_voiced` when the chords are exported as played, none for the original voicing, `_combined` for the Combined file (e.g. `..._voiced_combined.mid`). The name never depends on Chord Density settings. Melody-only keeps `Melody.mid`. No gated export exists, so no `_gated` name.
- Safety: Windows-forbidden characters (including the `/` of slash/inversion chords) become `_`; total name at most 150 UTF-16 units including suffix and `.mid`; longer progressions are cut at a chord boundary and end in ` - +N more [hash]` (FNV-1a, 6 hex digits of the full name); never cut inside a character; the drag file goes into its own folder per drag so it is never overwritten while the receiver reads it (folders older than a day are removed); the file chooser warns before overwriting.
- Metadata: Chords and Combined files carry SMF text events (FF 01, ASCII, tick 0) "Absolute Progression: ...", "Roman Progression: ...", "Project Key: F minor" whatever the filename format; the note events are unchanged, and the melody-only file stays free of chord data.
- UI: custom Export MIDI dialog (range, export type, melody channel, chord voicing, Filename Format segmented selector, live proposed name with ellipsis and tooltip); the main window has a "Name:" selector beside Export and a one-line preview of the drag file name. Edits, Dice rolls, key/mode changes, inserts/deletes, region and export-type changes all refresh the preview.
- Tests (`test_filename.cpp`): A/B/C exact names, chromatic numerals in both modes, D dice, E key change, F density independence, G/H ordering and repeats, region/type suffixes, slash safety, I persistence/legacy, J notes identical across formats, metadata, K long names (length, UTF-8 validity, suffix, determinism); mutation-checked (sorting, chromatic spelling, hash, sanitising).

## 4q. Chord length editing and "start on 1"
- **Resize bug (root cause).** `resizeBlockEnd` never grows a chord into its neighbour, and chords sit back-to-back by default (a progression is a row of touching bars), so dragging a right edge outward did nothing; the shared boundary also belonged to the *next* chord's left edge, which cannot move either. Nothing is wrong with the hit zones or the preview/commit flow.
- **Fix.** `resizeBlockEndRipple` (Timeline.h): growing past the next chord pushes the following chords later, keeping their order, lengths and any gap they do not need to give up; nothing is overwritten or shortened; the tail stops at the timeline end. Shrinking leaves a gap, as before. The grid's right-edge drag uses it, and grabbing the boundary between two touching chords now drags the earlier chord's right edge (edge grab width 8 -> 10 px). One undo step ("Resize chord").
- **"Chord length" button** (under the chord palette): a menu of 1/16, 1/8, 1/4, 1/2, dotted 1/2, 1, 2, 4 bars. With chords selected it sets each one's exact length through `setBlockLength` (later chords pushed, one undo step "Chord length", refused with a status message if the tail would not fit); with nothing selected it sets the length of newly inserted chords (same as the "Insert:" box). Its label shows the current length.
- **Start on 1.** `PlaybackScheduler` anchors the pattern where the transport starts playing: that host tick is tick 0 of the pattern, so space bar at bar 65, at bar 3 beat 2.5 or anywhere else plays the first chord first and repeats every pattern length (sample-accurate wrap as before). Locating while playing keeps the anchor; stop then play re-anchors; a start inside a count-in anchors at host position 0. This replaces the previous bar-aligned phase (bar 65 played chord 1, bar 66 chord 2); with a start at an exact multiple of the pattern length nothing changes. There is no on/off switch yet.
- Tests: ripple/`setBlockLength` cases in test_timeline.cpp (including the reported back-to-back case), start-on-1 cases in test_scheduler.cpp (many start positions, one bar to the second chord, locate keeps phase, restart re-anchors, count-in); mutation-checked (anchoring at 0 fails them).
- **Follow-up (playhead).** The audio followed the anchored pattern, but the editor's playhead was still folded by a separate bar-aligned `fmod`, so pressing play at bar 55 showed chord 3 (54 bars = 2 mod 4). The playhead now asks `PlaybackScheduler::patternTick()`, the same fold the audio uses, so the grid shows what sounds. Tested with the bar-55 case.

## 4r. Optimization pass (measured)
- Method: a benchmark on a 128-chord project (density 8, voice leading, 581 melody notes) timed the engines and fingerprinted their outputs (notes, serialized JSON, exported MIDI, Dice picks) before and after. Results: generateNotes 0.35 ms, voiceProgression 0.29 ms, generateProjectNotes 1.2 ms, serialize 0.5 ms, deserialize 1.1 ms, exportProject 1.8 ms, Dice 24 us per chord, scheduler 2-3 us per 256-sample audio block (budget about 5300 us). No hotspot, so no speed-motivated rewrites: they would add risk for nothing measurable.
- Changed: `AuditionPlayer::process` returned a freshly allocated vector on the audio thread; it now fills a reserved member buffer (max 128 voices, test pins that the storage is reused, mutation-checked). Two by-value vector parameters in MelodyEdit became const references. Removed the now unused `loopLengthPpq`. All output fingerprints identical before and after; 322 tests pass under g++ and clang -Werror.
- Looked at, left alone: UI timers are 15-30 Hz and repaint only when the playhead moved; mouse-drag previews copy the progression (about 1 us); clang-tidy's remaining performance hints (enum sizes, push_back inside tiny loops) are noise at these sizes.
- Known real-time caveat, unchanged: `MidiBuffer::addEvent` may allocate inside the host's buffer when it is full; the scheduler's event buffers are reserved from the note count, which holds unless the pattern loop is shorter than one audio block.

- User report (Live, latest build): "Works great" for the start-on-1 / playhead fix, given without per-step detail; the chord-length drag and button were not itemised separately.
