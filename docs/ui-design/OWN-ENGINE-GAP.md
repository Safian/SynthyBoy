# The own engine against the UI: what the iOS app shows and what it can do

Date 2026-11-12, branch `ios-ui`. Owner decision (question 110): SynthyBoy on iOS has the **same UI as everywhere**, drawn by the plug-in through the bitmap GUI extension
(`BITMAP-GUI-EXTENSION.md` section 14), with the **own engine** (`core/engine-own`, no Vital-derived code, `ARCHITECTURE.md` 2.2). The own engine (state after the parity work, branch `engine-own-impl`) acts on the keys of the generated table below (the spike's engine acted on 7 of 2637). This file is the work list for the own-engine parity task: every area of the UI, how much of it the own
engine implements, and what is missing. The table is generated (`scripts/gen-own-engine-gap.py`, numbers from `synthyboy_paramdump --own-engine-gap`).

## 1. What the UI does with the own engine (verified)

The UI asks the engine two things, and only the first changes what is drawn:

| Mechanism | Source | Effect in the UI |
|---|---|---|
| Capability dimming | `IEngine::capabilities ()` / `capabilities2 ()` against `requiredCaps ()` / `requiredCaps2 ()` of each key and of each value of a choice (`shell-juce/ui/model/ControlModel.cpp`, `capStateForKey`, `capStateForValue`), rule R5 of `13-new-keys.md` | A key whose bits the engine does not report is **drawn dimmed with the tooltip "not available in this engine"**; it stays visible, editable and stored (it is not hidden, not disabled, not removed from the page). Same for the entries of a choice list that need a bit, and for the matrix sources and destinations of the blocks above. |
| `IEngine::supportsParam ()` | the engine | **Not read by the UI.** It is the "own engine" column of `PARAMETERS.md` and the `ownEngine` field of `params.json`. |

So three kinds of key exist with the own engine, and the table in section 2 counts them:

* **acts** (7 keys): `master.volume`, `master.bend_range`, `master.transpose`, `master.tune`, `osc1.wavetable` (Sine / Saw), `filter1.on`, `filter1.cutoff`. Live and audible.
* **dimmed** (1757 keys): the keys added after state version 2 (the blocks of `13-new-keys.md`). The own engine reports one capability bit (`kCapEventContract`: sustain pedal and the per-note
  tuning expression) and none of the second word. Drawn dimmed, tooltip, value stays editable.
* **editable and without effect** (873 keys): the keys of state version 2 (everything Vital has: the oscillators, filter, envelopes, LFOs, matrix, the nine effects). They predate the
  capability words, so every engine "knows" them, **the UI draws them like live controls**, and the own engine ignores them. This is the real finding: on iOS a user can turn
  the level, warp, envelope and every effect knob of the main page and nothing happens, with nothing in the picture that says so. `09-kick-integration.md` (own engine flag) planned
  "Kick dims parameters with `ownEngine: false`"; the bitmap GUI and the UI kit do not implement it. Question 134 asks for the decision; until it is answered nothing is changed.

What was checked (all with the own engine, the same code as the iOS archive):

| Check | Where | Result |
|---|---|---|
| Every top level page (OSC MIX FLT ENV LFO MOD FX GLB) renders and differs from the one before, phone 390 x 844 and tablet 1024 x 768, the frame settles afterwards | `bg_every_tab_renders_phone_and_tablet_whatever_the_engine_can_do` (`tests/bitmap-gui/test_bitmap_gui.cpp`) | passes: macOS arm64 (ctest) and the iOS 26.5 simulator (arm64) |
| The 31 tests of the bitmap GUI (drag a knob, text input, files, remote pages, undo, two fingers, resize, scale, menus, several instances, race with the audio thread, ...) | `synthyboy_test_bitmap_gui` | passes with the own engine on macOS arm64 and in the simulator; one assertion about SOUND is own-engine aware (below) |
| Golden pictures at 390 x 844, 1024 x 768, 1024 x 600 (+ the FLT page and scale 2) with the capabilities of the own engine | `tests/bitmap-gui/golden-own/` (`golden/` stays the Vital engine's) | the simulator build reproduces the macOS pictures to 0.0000 % (FLT page 0.06 %, a pixel of anti-aliasing) |
| No crash, no assertion, no leak on tapping a dimmed control | the drag / tap tests above act on dimmed and inert controls too (the osc 1 level knob is inert with the own engine) | passes |

The pictures: `golden-own/phone-390x844.sbg` (the reference screenshot of the own engine at the phone size; written as PNG with `SYNTHYBOY_BITMAP_GUI_DUMP_DIR`). In it the macro knobs
M5 .. M8, the sub oscillator ("MUTED / OFF") and the remap editor are dimmed; the oscillator page knobs (frame, warp, morph, level) look live and are inert.

Two assertions of the tests depend on the engine and say so with `SYNTHYBOY_HARNESS_OWN_ENGINE`: the level drag changes the CLAP parameter on every engine, the SOUND follows only on
the Vital engine (with the own engine the test asserts that it does not, so the day the own engine implements `osc1.level` the test fails and is changed on purpose); the meter of a sounding note
moves for fewer frames (the fixed envelope settles after 150 ms): 4 .. 6 frames measured instead of 5 or more.

## 2. The table (generated)

Areas are the groups of the parameter table (`group` field of `params.json`), `Editable, no effect` is the column that has no marker in the picture. Regenerate with
`scripts/gen-own-engine-gap.py build/tools/paramdump/synthyboy_paramdump`; `--check` fails when this file is out of date.

<!-- BEGIN GENERATED TABLE (scripts/gen-own-engine-gap.py) -->

| UI area (panel) | Keys | Own engine acts | Drawn dimmed (capability missing) | Editable, no effect | What is missing |
|---|---:|---:|---:|---:|---|
| Top bar: master volume, bend range, transpose, tune | 11 | 11 | 0 | 0 | Acts: volume, polyphony, bend range, velocity tracking, transpose, tune, legato, poly legato, bend down range / link, stereo spread, stereo routing. Missing: the velocity and note curves (legato options). |
| Master / Portamento (Glide) | 4 | 4 | 0 | 0 | Acts (time, slope, always, scaled by interval). |
| Master / Curves, Voicing | 7 | 4 | 3 | 0 | Acts: same note rule, steal priority, voice override Kill (50 ms fade) / Steal, note recall, stereo routing. Missing: velocity and note curves. |
| Master / Scale, Tuning, MPE | 13 | 13 | 0 | 0 | Acts: key scale conforming, concert pitch A4, Pythagorean / Just / user Scala tuning, the MTS-ESP seam, MPE (member channel bend, axes to macros, Y to mod wheel, bend to X). |
| Quality | 2 | 0 | 2 | 0 | 4x oversampling and the no-smoothing switch (warped oscillators run 4x oversampled internally). |
| Voice Control | 62 | 62 | 0 | 0 | Acts: per voice pan / detune / cutoff / envelope time steps, randomisation, scaling, the oscillator mask, the three sources. |
| Oscillators: wavetable choice, frame, warp, level, pan, pitch | 81 | 45 | 36 | 0 | Acts: oscillators 1 .. 3 (level, pan, pitch, frame, phase, destination, env 1). Missing: the pitch modes, transpose snap, fixed pitch, filter blend and sends, the frame remap, smooth interpolation. |
| Oscillators: wavetable table, unison, spectral morph, warp lists | 93 | 69 | 21 | 3 | Acts: the 15 built-in tables and user tables (Preset table, from the content store), the spectral morph (all 11 types), unison (voices, detune, curve, blend, spread, mode, range), warp 1 and 2 (Sync .. Squeeze, FM / RM / PD / AM from the other oscillators, the Noise, the Sub and the filters, bends, asymmetry, Remap 1-4, Odd/Even, Filter LPF / HPF, the distortion warps, the 23 spectral warps on the Spectral type), key and velocity mapping, Ratio SRC, smooth interpolation. Missing: the Serum unison law, stack styles, transpose snap. |
| Oscillators: sample, granular, spectral, multisample types | 255 | 240 | 0 | 15 | Acts: the Sample type (pitch, start, loops, slices, scan, normalize, fade edges, unison), the Granular and Spectral types (all keys incl. the X|Y control, the filter mask and wavetable filter, Mask / Vocode from the other oscillators and the Noise), the Multisample type (SFZ zones, envelope override, timbre, release triggers, key switches, choke groups of the slot). Missing: nothing of this area. |
| Sub oscillator | 14 | 11 | 2 | 1 | Acts: waveform, octave, tune, level, pan, destination, env 1, phase. Missing: direct out, contiguous phase, bus sends, filter blend. |
| Noise oscillator | 19 | 19 | 0 | 0 | Acts: colours, level, pan, destination, env 1, stereo. Missing: the sample noise type, pitch, fine, phase, keytrack, loop, bounce. |
| Mixer (buses, sends, destinations) | 8 | 8 | 0 | 0 | Acts: filter blend, sends, Bus 1 / 2 (rack, level, destination, bypass), Direct, Main bypass. |
| Filter 1 (cutoff, on) | 17 | 17 | 0 | 0 | Acts: on, model, style, cutoff, resonance, drive, blend, mix, keytrack, var, pan, level, clean mode, env 1, destination, bus sends (the models map onto the filters-serum types; Diode, Formant, Comb, Phaser written here). |
| Filter 2, FX filter, model settings | 49 | 40 | 0 | 9 | Acts: filter 2 (same keys), routing, the formant / comb offset settings, the FX filter (without pan, clean mode, keytrack). |
| Envelopes 1 .. 6 | 96 | 96 | 0 | 0 | Acts: all times (ms or beats), sustain and the three curves (squared amplitude, DAHDSR), legato inverted. |
| LFOs 1 .. 10, random LFOs | 202 | 202 | 0 | 0 | Acts: the 12 shapes and the custom shape with point modulation, rate, tempo sync, key follow, all six sync types, delay, fade, smooth, direction, swing, stereo offset, host lock, mono, the five random styles (stereo). |
| Macros 1 .. 8 | 8 | 8 | 0 | 0 | Acts. |
| Modulation matrix (64 slots) | 1024 | 1024 | 0 | 0 | Acts: source, destination, amount, power, bipolar, stereo lanes, bypass, aux source (inv, power, output), rise / fall / delay of a slot, the amount of a slot as destination, per slot curves, cycle check. |
| FX chain: order, racks, slots (16 instances) | 480 | 480 | 0 | 0 | Acts: the order (a repeated choice is repaired), the racks Main / Bus 1 / Bus 2 and the bands of the splitters, the pool of 16 instance slots (fxiNN: type, on, rack, order, 24 modulatable values, 10 ms crossfade) and the latency compensation between the paths of the mixer. |
| FX: chorus, flanger, phaser | 35 | 35 | 0 | 0 | Acts: chorus, flanger (the intended design of document 06), phaser (12 stages, depth 2, blend, feedback). |
| FX: delay, reverb | 37 | 37 | 0 | 0 | Acts: delay (all styles, sync, tape glide, filter, offset, hq), reverb (the six algorithms with room, ER size, diffusion A / B, feedback, width, force enable). |
| FX: compressor, distortion, EQ | 54 | 54 | 0 | 0 | Acts: compressor (multiband, upward and downward stages, Limit mode with look-ahead, Serum mode, crossovers, limiter latency), distortion (all 14 types with the filter key track), EQ (Vital and Serum law, second peak band). |
| FX: hyper dimension, utility, Bode, convolve, splitters | 66 | 64 | 0 | 2 | Acts: Hyper/Dimension, Utility, Bode, Convolve (impulse of the content store, both laws), the three splitters with their band racks. Missing: hyper / bode retrigger. |
| **All** | **2637** | **2543** | **64** | **30** | |

<!-- END GENERATED TABLE -->

## 3. What is not a key

| UI feature | Own engine | What is missing |
|---|---|---|
| Voices and polyphony | 8 voices, one oscillator each (saw or sine), `Quality::maxVoices` is the cap | unison; the 1 .. 32 voice count of `master.polyphony`; voice stealing rules; per voice variation |
| Envelope | one fixed envelope in the voice: 5 ms attack, 150 ms decay, 0.7 sustain, 250 ms release | six editable envelopes; the envelope display of the UI shows the editable ones; routing |
| Modulation | none (no sources, no matrix, no macro routing, no aux, no rise / fall / delay) | the whole modulation system; the knobs' live modulation readout (`IEngine::copyReadout` answers false: the knobs never move on their own) |
| Effects | none | the 16-slot rack, the base effects (chorus, compressor, delay, distortion, EQ, flanger, phaser, reverb, FX filter) and the Serum-class effects (Hyper, Bode, Convolve, splitters, Utility), with their tails |
| Wavetables and content | `osc1.wavetable` 0 = sine, anything else = saw; `setContent`, `setContentStore` are ignored | wavetable scanning and morphing, user tables, samples, impulse responses, curves, remaps, custom LFO shapes; the wavetable view of the UI draws the real table, the voice plays a saw |
| Tempo and transport | `setTransport` is ignored | tempo sync of LFOs, delays, envelope beat units, HOST mode |
| Tuning, scales, MPE | per note tuning expression and master tune / transpose only | scale conforming, Scala / TUN / KBM tunings, MTS-ESP seam, MPE routing |
| Quality tiers | the voice cap only | oversampling, interpolation quality, the offline render at the high tier (`renderOffline` is not implemented: the shell builds the twin from outside) |
| Presets | the 112 factory presets, imported Vital / Serum presets load, every key is stored and saved | they sound like a saw through a low pass: the preset browser of the UI works, the sound is not the preset's (the importer says which keys an engine cannot reproduce, `11-vital-preset-import.md`) |
| Live displays that do not depend on the engine | peak meter, voice counter, CPU load, scope ring are computed from the plug-in's output in `process ()` (`synthyboy_clap.cpp`) | nothing |
| Latency | 0 | (Bode needs a Hilbert latency: with the effect) |

## 4. The work list for the own-engine parity task

Ordered by what a user of the main page notices first. Every item is finished only when it has engine, capability bit (set ONLY when an off / on render test proves an audible
difference, rule R5), the render test, and the entry in `docs/PARAMETERS.md` ("own engine" column, generated) and in the table above (`scripts/gen-own-engine-gap.py`); clean-room
discipline of `ARCHITECTURE.md` 2.2 applies: no copying from `core/engine-vital/`, the contract is the `ParamInfo` semantics plus the render tests.

1. **The state of version 2 first, because the UI shows it as live**: oscillator level / pan / pitch / frame / warp, the three oscillators, the voice envelope as `env1` (ADSR + curves),
   `filter1` resonance / drive / mix / model, `filter2`, the filter routing; unison; `master.polyphony`, velocity tracking, legato, portamento.
2. The modulation system (sources, 64 slots, macros 1 .. 8, LFO 1 .. 8, random LFO, envelopes 2 .. 6, `copyReadout` for the knob rings) with the capability bits `kCapMacros58`,
   `kCapLfo910`, `kCapModSourcesExtra`, `kCapModAmount`, ... in the order the table shows them.
3. The effect chain (the base effects, then racks / 16 slots, then the Serum-class effects) with `kCapFxRacks`, `kCapFxInstances`, `kCapFx*`.
4. Wavetables as data (scanning, user tables, remap, warp modes 1 .. 96), the sample / granular / spectral / multisample oscillator types (their libraries are already part of the iOS
   build; the own engine does not call them), sub and noise.
5. The mixer graph, filter models and the rest of the capability words.
6. Parity measurement: `tools/ref-compare` and the parameter audit exist for the Vital engine only (they need it as the reference); the own engine's parity is judged against the
   same `ParamInfo` semantics and the golden / measurement tests, not against code similarity.

Until a key moves from "editable, no effect" to "acts", the decision of question 134 (dim it, or leave it) is the only thing that tells an iOS user the control is inert.

## 5. Open question for the owner

* **Question 134**: dim the keys of version 2 that `supportsParam ()` says the engine does not act on (the design of `09-kick-integration.md`), or leave them as live looking controls. Default used:
  nothing changed (the owner asked for the same UI as everywhere); the cost of dimming is one more rule in `capStateForKey` and a host query (`UiHost::supportsParam`).
* **The iOS rule and `core/wavetable-edit`**: `libsynthyboy_wtedit.a` (part of the core on every platform, so of iOS) contains the namespace `synthyboy::wtedit::vital` ("Vital Group /
  Component / Keyframe non-UI data model", `core/include/synthyboy/wavetable_edit.h`). `scripts/check-no-vital.sh` matches `vital::` and `Vital`, and the iOS CI job has only ever scanned
  `libsynthyboy_core.a` and `libsynthyboy_static.a`, not this archive. The new static UI library links it and the UI refers to it, so the script now allows `wtedit::vital::` and the importer's UI texts
  (the list is in the script, with a comment). Whether that model is original code (it models the group / component / keyframe structure of the Vital wavetable FORMAT) is a licence
  question that this branch does not settle; the file is in the core since the wavetable editor exists and the rule of 2.2 is "no Vital-derived code in iOS builds".
