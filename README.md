# ChordSketch

A Hookpad-inspired chord sequencer, built as a Windows VST3 plugin (C++17, JUCE, CMake). Primary
host: Ableton Live 12.4.16. "ChordSketch" is a placeholder name.

**Status: milestone 1 complete (verified in Ableton Live 12.4.16 by the user); milestone 3 (live MIDI output synced to the transport) also complete and confirmed working in Live by the user.** MIDI export comes first; the timeline UI and live playback come later.
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
| Live MIDI output from `processBlock`, synced to the host transport | Core scheduler **unit-tested** (81 tests incl. start, stop, locate, loop, tempo, exact sample positions, agreement with the exported file); plugin wiring **syntax-checked only** | **Works in Live 12.4.16** (user-reported: MIDI routed to another track plays correctly). Checklist below |
| Project state: versioned JSON save/restore, undo/redo (snapshot history with coalescing), chord-text entry | Core **unit-tested** (117 tests total with g++ Debug and clang Release, `-Werror`; mutation-checked: 8 injected bugs all caught). Plugin wiring (host save/load, thread hand-over, editor) **syntax-checked only until CI builds it** | **Tested in Live 12.4.16: user reports the milestone 4 checklist completed with no errors** (per-step results not itemised) |
| Timeline UI after the supplied mockup: coloured chord cards, 7-chord palette, Triad/7th/Sus2/Sus4, scale switch, move/resize/split/duplicate/delete/add with grid snapping, playhead | All editing rules **unit-tested** in core (Timeline, ChordEdit: 145+ tests with mutation checks). Painting and mouse handling **syntax-checked only until CI builds it**; I could not render it here | **Tried in Live 12.4.16: user reports "It works"** (per-step results not itemised); checklist for milestone 6 below |
| Preview button: plays the progression from bar 1 at the project tempo while the DAW is stopped | Core clock **unit-tested** alone and driving the real scheduler (exact sample positions, no stuck notes, host takes over, edit while previewing; 6 mutants caught). Audio-thread wiring and button **syntax-checked only until CI builds it** | **Not yet tried in Live** (checklist below). Main risk: Live must keep calling the plugin while stopped |
| Project length (Auto / 4 / 8 / 16 / 32 bars) and chord octaves (whole project and per chord, each -2..+2) | **Unit-tested** in core: clip length in the exported file, exact 12-semitone shifts through the single note engine, saved state (written only when set, so old files and the pinned format are unchanged), range safety checked exhaustively over all keys/chords/inversions; 14 mutants caught. UI wiring **syntax-checked only until CI builds it** | **Not yet tried in Live** (checklist below) |
| Drag-out of `.mid`, 9th chords | Not implemented (milestones 7 and 5). The "9th" button is shown disabled | |

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
- Milestone 4: `test_serialization` (golden JSON pinning the on-disk format, exact round trips including
  tempos with no short decimal form, 300 random progressions, dozens of hostile inputs: truncation, wrong
  format/version, bad enums, overlaps, duplicate keys, nesting bombs, binary junk, 2000 single-byte
  corruptions), `test_document` (undo/redo, coalescing, rejected edits change nothing, bounded history,
  randomised undo-everything/redo-everything), `test_progressiontext` (chord line parser/formatter and the
  Roman numeral formatter as an exact inverse of the parser, exhaustively), and scheduler tests for editing
  while notes are sounding.
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

## Manual test checklist: milestone 3, live MIDI output (Ableton Live 12.4.16) — user reports it works perfectly

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

## Manual test checklist: milestone 4, state and undo/redo (Ableton Live 12.4.16, Windows) — user reports complete, no errors

Install the new build (replace `C:\Program Files\Common Files\VST3\ChordSketch.vst3`, Live closed).

1. Open the plugin window. Expect a chord line `I V vi IV`, key C major, tempo 120, "4 chords, ... notes".
2. Click into the chord line and type, e.g. `I vi IV V7:2`, then press **Enter** or click **Apply**.
   *If typed keys do not arrive at all inside Live, tell me: it is the main open risk of text entry in a
   plugin window. The Key, Tempo, Undo and Redo controls do not need the keyboard.*
3. Play from bar 1: the new chords play (key and tempo changes take effect on the next play or immediately
   while playing; notes sounding during an edit are released, not stuck).
4. Change **Key** to D minor and drag the **Tempo** slider. Press **Undo** once: the whole tempo drag reverts
   in one step. Undo again: the key reverts. **Redo** reapplies.
5. Type nonsense such as `I X V` and Apply: the status line explains and nothing changes.
6. **Save the Live set, close it, reopen it.** The plugin must show the same chords, key and tempo, and play
   them. Undo is empty after reload (history is not saved by design).
7. Duplicate the track: the copy must carry the same progression. Edit one copy: the other must not change.
8. Delete the plugin and add a fresh one: it starts with the demo progression.

Not covered here: saved states from other versions (only version 1 exists); a newer file in an older
plugin is refused with a message and the current project is kept.

## Manual test checklist: milestone 6, timeline UI (Ableton Live 12.4.16, Windows) — user reports it works

Install the new CI build as before. The window is 640 x 620 and not resizable.

1. Open the plugin: dark window, header, Key/Scale/Meter, four coloured cards (C major: I, V, vi, IV), a palette of seven coloured chords. **Please send a screenshot**: I could not look at it, so layout and colours are unverified.
2. Click each card: it gets a white outline; the name (top right of "Selected chord") and the pills update.
3. Click palette chords: the selected card changes numeral, colour and chord name. In minor the palette reads i, ii°, III, iv, v, VI, VII.
4. Change **Scale** to Minor: diatonic chords follow (I V vi IV becomes i v VI iv); **Key**: names change, flats vs sharps follow the key.
5. **Triad / 7th / Sus2 / Sus4**: toggles work, Sus is greyed on diminished chords, **9th** is intentionally disabled.
6. **Drag a card's right edge** to shorten it (gap appears with a faint "+"); drag it back; drag a card's body between neighbours (it stops at them, never jumps over); try Snap: Beat / Off.
7. **Click a "+" slot**: a tonic chord appears there. **Split**, **Duplicate** (needs free room after the card), **Delete**.
8. **Undo** after each of the above: one step each, drags are one step. Redo works.
9. Press play in Live: the white **playhead line** follows the transport; MIDI still plays the edited chords (route as in the milestone 3 checklist), and editing during playback leaves no stuck notes.
10. **Copy progression**, then **Paste** after editing: the progression returns. Save the set, reopen: the edited project is restored.
11. **Export MIDI...** still writes a file that matches what plays.

Known limits (superseded by the precision chord grid, see its checklist): the timeline always fits the whole progression in its width; the last card can be lengthened by dragging past the right edge or with Duplicate; the meter (4/4) is shown but not editable.

## Manual test checklist: Preview (Ableton Live 12.4.16, Windows) — not yet run

Same routing as the milestone 3 checklist (track 1 = ChordSketch, track 2 = an instrument with MIDI From = ChordSketch, monitor In). **Live's transport must be stopped.**

1. Click **▶ Preview**. Expect the chords to sound from bar 1 at the project tempo (the tempo slider, not Live's), the white line to move across the cards, and the button to read **■ Stop**.
2. It should stop by itself after the last chord, and the button return to Preview. No stuck note.
3. Click Stop in the middle of a chord: sound stops at once.
4. During a preview, edit (palette click, drag a card, drag the tempo slider): it keeps going with the change, no stuck note.
5. Press play in Live while previewing: the preview hands over to Live's transport (no doubled notes). While Live is playing, Preview shows a message instead of starting.
6. Compare with the milestone 3 result: Live-synced playback and Preview should play the same chords.

**If Preview does nothing while Live is stopped**, tell me: Live may stop calling a plugin that has no incoming MIDI while the transport is stopped, in which case Preview needs a different route (for example the track armed or monitoring).

## Manual test checklist: length and octaves (Ableton Live 12.4.16, Windows) — not yet run

1. **Length**: pick 8 bars. The timeline shows eight bar slots with the four cards at the left and numbered "+" slots to the right; 16 bars: narrower cards (below about 50 px a card shows only its numeral). Click a "+" slot to add a chord out there; Duplicate and drag-to-extend also work now.
2. **Export MIDI** with length 8: drop the file on a Live clip: it is an 8-bar clip with the chords at the start (empty bars at the end). Length "Auto" gives the old behaviour (clip just long enough).
3. **Octave** (header): +1 and -1 move every chord up/down an octave when played (route MIDI to an instrument as before) and in the exported file; 0 is the original range (roots in C2-B2 in Live's naming).
4. **Selected chord octave** (the - and + next to the chord name): moves only that chord; the card shows "Triad · +1"; the buttons stop at +2 / -2 with a message.
5. Both add up (project +1 and chord +1 = two octaves up). **Undo** reverts each of these in one step.
6. **Save the Live set, reopen it**: length, octaves and per-chord octaves are restored.
7. Copy / Paste progression only carries chords and lengths, not octaves: pasted chords come back at octave 0 (known limit).

## Manual test checklist: precision chord grid (Ableton Live 12.4.16, Windows) — user reports it works (no per-step detail given)

Nothing below is claimed to work in Live until you have run it. The tick maths, snap, ruler, zoom/scroll, group edits, MIDI export, save/reload and undo are covered by automated tests (tests/test_grid.cpp, tests A-I); the mouse/keyboard behaviour and the host checks are not.

1. Open the plugin: a ruler (bar numbers, beat numbers, "&" lines) sits above one chord lane; Snap is 1/8, Insert is 1/4.
2. Pick a chord in the palette with nothing selected (press Esc in the grid first): the status line says it is armed. Hover the lane: a faint ghost shows where a click would insert. Click the "&" of beat 2: a one-beat chord appears there; the status line reads "Bar 1, beat 2 &".
3. Change Snap to 1/16 and add a chord on the "a" of beat 3; change to 1/8 triplet and add one; chords already placed must not move. Change Insert to 1/2 or 1 bar and insert again.
4. Drag a chord: the original stays as an outline, a ghost follows the snapped position and a label shows the destination bar/beat. Release, then Undo: one step undoes the whole drag. Drag the left and right edges to resize; a chord never reaches zero length and stops at its neighbour.
5. Click, Shift-click, and rectangle-drag on empty lane to select several; drag one of them: all move together. Try Ctrl+D, Delete/Backspace, Esc, Ctrl+A. **Keyboard delivery inside Live's plugin window is unverified**; the Duplicate and Delete buttons do the same.
6. Ctrl+wheel zooms about the cursor; Shift+wheel and the scrollbar scroll; Fit shows the whole project. At low zoom the small labels disappear but chords stay put.
7. In Live: record or play the MIDI output to another track and check that the chord on the "&" of beat 2 sounds half a beat after beat 2. Export MIDI and import it into a clip: first note at the "&" of beat 2, length one beat.
8. Press play in the DAW, loop a region, edit chords while it plays: no hanging notes; the playhead and its "Bar x, beat y" readout follow the transport (also after a tempo change).

Known limits: there is no Chord Density feature in this codebase, so no density indicator or density test exists (Test F covers 7th, inversion and octave voicing instead); the editor window is not resizable; overlap policy is "never overwrite" (see docs/PLAN.md 4g).

## Manual test checklist: M7, drag MIDI out (Ableton Live 12.4.16, Windows) — user reports it works perfectly

Confirmed by the user in Live (no per-step detail given).

1. Footer: a "Drag MIDI" pill sits where the old note was. Click it once: the status line explains what to do.
2. Press and drag it out of the plugin window onto an empty MIDI track or a clip slot in Live: a clip should appear with the progression (same notes and timing as Export MIDI...). Try the arrangement view too.
3. Drag it into an Explorer folder: `ChordSketch.mid` appears and opens/imports correctly.
4. Edit a chord, drag again: the new file reflects the edit (the file is rewritten at every drag).

If Live refuses the drop, say what happens (nothing, forbidden cursor, or an error) and I'll adjust; Export MIDI... remains the fallback. The file is written to the Windows temp folder, `ChordSketch\ChordSketch.mid`.

## Manual test checklist: M5, 9th chords (Ableton Live 12.4.16, Windows) — user reports it works

Theory, names, numerals, edits, save/reload and MIDI export of 9ths are covered by automated tests (tests/test_ninth.cpp); the button and display are not yet seen in Live.

1. Select a chord and press **9th**: it adds the diatonic 7th and 9th (C major: I = Cmaj9, V = G9, ii = Dm9, iii = Em7b9 with a flat 9, vii = Bm7b5b9). The card shows "9th" and the Roman numeral reads e.g. V9; five notes sound, the 9th on top.
2. Press **7th** on a 9th chord: it drops to the plain 7th chord. **Triad** clears both. **9th** again removes just the 9th.
3. Change the chord with the palette while a 9th is on: the 9th follows the new degree (V9 to iii gives a flat 9). Sus2/Sus4 combine (G9sus4).
4. Switch Scale to Minor: the 9th is retargeted to the minor scale. Save the project, reload: 9ths come back. Undo/Redo each step.
5. Export or drag the MIDI out: five notes per 9th chord.

Not included: 11ths, 13ths, other alterations, or 9th inversions (a 9th chord is root position only). Minor-key numerals stay natural-minor style (i, III, VI, VII).

## Manual test checklist: inversions (Ableton Live 12.4.16, Windows) — user reports it works

Voicing, limits, names, numerals and edit rules are covered by automated tests (tests/test_inversion.cpp); the buttons are not yet seen in Live.

1. Select a chord: the **Selected chord** row now has an inversion control (`-  Root pos.  +`) left of the octave control.
2. Press **+**: C major goes C/E (1st inv.), then C/G (2nd inv.). The card name shows the bass note and its small label reads "inv 1"; the notes you hear are the same chord with the bass moved up. **-** goes back; the buttons grey out at the ends.
3. A 7th chord goes up to the 3rd inversion, a 9th chord up to the 2nd (V9 inverted reads V9(1), V9(2)). Adding a 7th/9th to an inverted chord keeps the inversion where allowed; Triad clamps it to 2nd.
4. Select several chords (Shift-click) and press **+**: all of them change together, one Undo step. Changing the chord in the palette keeps its inversion.
5. Export/drag the MIDI out: the inverted voicing matches what plays.

## Manual test checklist: combined build (copy/paste, bar-range export, 11th/13th/alterations, audition, resizable window) — not yet run

All of the music logic here is covered by automated tests (test_progressiontext, test_midifile [range], test_extensions, test_audition). What is NOT yet seen in Live: the buttons, the dialog, the audition sound and the window resizing. Please report each step.

1. **Copy / Paste.** Build a progression with a 9th chord, an inversion, a chord octave and a gap. **Copy progression**, change the project, **Paste**: octaves, inversions and 9ths come back (text such as `V9(1):2@-2`).
2. **Export a bar range.** Press **Export MIDI...**: a small window asks "From bar / To bar" (pre-filled with your selection's bars, or the whole project). Export bars 2-3 only, drag the file into Live: it starts at its own beat 1, and a chord that crosses the range edge is cut there. The exact rules are in core/include/chordcore/MidiFile.h.
3. **Extensions.** New row under the chord buttons: **11th, 13th, b9, #9, #11, b13**. Select a V chord and press 13th: it adds the 7th and 9th, giving G13 (no 11); #11 gives G13#11; b9 / #9 change the 9th; pressing an alteration again undoes it. Names and numerals follow (G7#9, G9b13, V13). 11th/13th chords are root position only (the inversion buttons grey out). With both octave settings at +2 a B-rooted 13th would pass MIDI 127: the edit is refused with a message.
4. **Palette audition.** With the DAW stopped, click a palette chord: you hear it briefly (about 0.8 s) from the track the plugin's MIDI goes to (same routing as Preview). It must not play while the DAW plays or Preview runs, and no note may hang.
5. **Resizable window.** Drag the window's bottom-right corner: it grows (minimum 640 x 716); the extra height goes to the grid. Check that Live lets you resize it. The size is not remembered between sessions.

## Manual test checklist: melody piano roll, Build A (Ableton Live 12.4.16, Windows) — user reports everything works (no per-step detail given)

What this build adds: a colour-coded melody piano roll under the chord grid. Backgrounds show how each pitch relates to the chord that is active at that moment; note colours show the scale-degree identity. Playback sends chords on MIDI channel 1 and the melody on channel 2. **MIDI export is still chords-only in this build** (the Chords/Melody/Combined export comes in Build B). The melody generator comes in Build C.

1. Load the plugin on a MIDI track with an instrument after it. Under the chord grid there is a "Melody" row (colour mapping, palette, labels) and an empty piano roll with chord names along its top edge.
2. Look at the backgrounds with the transport **stopped**. In C major over a C chord, C/E/G rows are bright, the other white-key rows faint, black-key rows hatched. Chord boundaries are exact vertical lines that line up with the chord cards above.
3. Click an empty cell: a note appears (and sounds through your instrument). Its colour should be C red, D orange, E yellow, F green, G blue, A purple, B pink (Classic palette, C major).
4. Change a chord (palette or inversion/extension buttons). The backgrounds under that chord change; the note colours do **not**.
5. Drag a note sideways and up/down; drag its left/right edge to resize; drag empty space to rectangle-select; Shift-click toggles; Ctrl+A, Delete, Ctrl+D, Ctrl+C / Ctrl+V (pastes at the pointer), arrow keys (Shift = octave) all work. Ctrl+wheel / Shift+wheel zoom and scroll both lanes together; plain wheel scrolls pitch.
6. Select notes: the Velocity slider changes them (brighter = louder); Lock marks them (a small bar on top). Undo/Redo (the tools row) reverts every one of these edits.
7. Change Key (e.g. to F minor, or A minor) and the Colours / Palette / Labels boxes. Labels: degree, pitch, both, none.
8. Press play in Live: melody notes sound on channel 2 together with the chords. Save the Live set, reopen it: notes, locks, colour settings are back.
9. Open a project saved by an older build: it loads with an empty melody and chords unchanged.

Please tell me what does not match, including anything that looks or feels wrong even if it works.

## Manual test checklist: Build B, melody export and mutes (Ableton Live 12.4.16, Windows) — not yet run

What this build adds: **Export MIDI...** now asks what to export (Combined / Chords only / Melody only) and whether the melody goes on its own channel or channel 1; the drag handle has a "Drag: both / chords / melody" choice; "Mute chords" and "Mute melody" silence a part in playback only. Exports ignore mutes.

1. Make a progression and a few melody notes. Press **Export MIDI...**, choose "Chords + melody", save `Combined.mid`. Drag it into two Live MIDI tracks or onto one: Live should show a Chords part (channel 1) and a Melody part (channel 2) with the same tempo, 4/4 and key. (How Live splits a Format 1 file on import is something I cannot check; please tell me what it does.)
2. Export "Chords only" (`Chords.mid`) and "Melody only" (`Melody.mid`). Each contains only its own notes; both have the same length so they loop together.
3. Export Melody with "Melody on channel 1 (compatibility)": the notes are on channel 1.
4. Choose a bar range (e.g. 2 to 3): both parts are cut to those bars and start at bar 1 of the new file.
5. Drag the handle with each "Drag:" choice onto a Live track: the matching file arrives.
6. Mute melody, press play: only chords sound. Mute chords instead: only the melody. Both off: both play. Mute state is saved with the project and each press is an undo step. Export still contains both parts while muted.
7. Route channel 2 to its own instrument in Live if you like (Live's MIDI-From channel setting on a second track). The plugin only sets the channel numbers; whether Live treats them as separate tracks is up to Live's routing.

## Manual test checklist: Build C, melody generator (Ableton Live 12.4.16, Windows) — not yet run

What this build adds: a **Generate...** button under the piano roll opens the generator panel (contour, density, rhythm, length, tension, register, seed). The generator is a deterministic rule-based engine: it favours chord tones on strong beats, scale tones on weak beats, small steps over leaps, phrase endings on the chord root, and only uses notes outside the key when Tension allows. It runs when you press a button (never while audio plays), and every run is one undo step.

1. Press **Generate...**, leave the defaults, press **Generate**. A 4-bar melody appears starting at the selected chord's bar (or bar 1). Play it: it should follow the chords (notes outside the chord are mostly on weak beats; the last note of the phrase lands on the chord's root).
2. Press **Reroll**: a different melody (seed + 1). Set the seed back to the earlier number and press Generate: the earlier melody returns exactly.
3. Try each Contour (Ascending, Descending, Arch, Inverted arch, Wave, Free), Density (Sparse/Moderate/Dense), Rhythm (Sustained/Lyrical/Rhythmic/Syncopated/Free) and the register Lowest/Highest sliders: every note should stay inside the register. Tension 0 = only notes in the key.
4. Select a few melody notes and press **Lock**. Generate again (or Reroll): locked notes stay exactly where they are and new notes avoid them. Undo (tools row) brings the previous melody back.
5. Select one or two chords (or notes) and press **Selected region** in the panel: only that span changes. **Whole project** regenerates the unlocked notes everywhere.
6. Change a chord (e.g. replace the V chord), look at the piano roll: the melody did **not** change by itself (some notes now sit outside the chord, shown by their backgrounds). Press **Regenerate affected** in the panel: only the changed chord's span is regenerated.
7. Save the Live set, reopen it: generator settings, seed, locks and notes are all back; "Regenerate affected" still knows which chords changed.
8. Export Combined.mid after generating: the generated notes are in the Melody track.

Not built: Chord Density and Chromatic Dice do not exist in this project, so there is nothing to wire the generator to for them.

## Licensing

JUCE is dual-licensed (AGPLv3 / commercial). Distributing a plugin built with JUCE requires either
releasing it under a compatible open-source licence or holding a JUCE commercial licence. Check
<https://juce.com/legal/juce-8-licence/> (and JUCE's `LICENSE.md`) before distributing binaries. This repository has no licence file yet.
