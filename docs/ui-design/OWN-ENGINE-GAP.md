# The own engine against the UI: what the iOS app shows and what it can do

Date 2026-11-12, branch `ios-ui`. Owner decision (question 110): SynthyBoy on iOS has the **same UI as everywhere**, drawn by the plug-in through the bitmap GUI extension
(`BITMAP-GUI-EXTENSION.md` section 14), with the **own engine** (`core/engine-own`, no Vital-derived code, `ARCHITECTURE.md` 2.2). The own engine is the spike's synth
plus the event contract: it acts on 7 of the 2637 keys the UI shows. This file is the work list for the own-engine parity task: every area of the UI, how much of it the own
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
| Top bar: master volume, bend range, transpose, tune | 11 | 4 | 4 | 3 | master.volume, bend_range, transpose, tune act. Missing: polyphony (the engine has a fixed voice cap from the quality tier), velocity tracking, legato, bend-down range / link, stereo spread. |
| Master / Portamento (Glide) | 4 | 0 | 0 | 4 | Pitch glide between notes: time, slope, always, scaled by interval. No glide in the voice. |
| Master / Curves, Voicing | 7 | 0 | 7 | 0 | Velocity and note curves, voice override (kill / steal), steal priority, same note rule, stereo routing. |
| Master / Scale, Tuning, MPE | 13 | 0 | 13 | 0 | Key scale conforming, concert pitch, Scala / tuning files, MPE routing (the own engine has only the per-note tuning expression and the pedal of the event contract). |
| Quality | 2 | 0 | 2 | 0 | 4x oversampling and the no-smoothing switch (the own engine has no oversampling). |
| Voice Control | 62 | 0 | 62 | 0 | Per voice pan / detune / cutoff / envelope time, randomisation, scaling, the oscillator mask. |
| Oscillators: wavetable choice, frame, warp, level, pan, pitch | 81 | 0 | 57 | 24 | Nothing acts: the oscillators 2 and 3, level, pan, pitch, frame position, warp amounts, the oscillator type switch (the voice has one oscillator and plays a saw or a sine, the wavetable the UI draws is not the one that sounds). |
| Oscillators: wavetable table, unison, spectral morph, warp lists | 93 | 1 | 45 | 47 | osc1.wavetable acts as Sine / Saw. Missing: wavetable scanning and the user tables, unison (voices, detune, blend, laws), spectral morph, the 96 warp modes, the oscillator mapping keys, the frame remap curves. |
| Oscillators: sample, granular, spectral, multisample types | 255 | 0 | 255 | 0 | The sample, granular, spectral and multisample oscillator types (the libraries synthyboy_sampleosc, _granular, _spectral, _multisample are in the iOS build, the own engine does not use them yet). |
| Sub oscillator | 14 | 0 | 14 | 0 | The whole sub oscillator (waveform, level, pan, octave, coarse, phase, direct out). |
| Noise oscillator | 19 | 0 | 15 | 4 | The noise generator (type, colour, level, pan, pitch, sample noise, keytrack). |
| Mixer (buses, sends, destinations) | 8 | 0 | 8 | 0 | The mixer graph: filter blend, buses, sends, direct output, oscillator destinations. |
| Filter 1 (cutoff, on) | 17 | 2 | 8 | 7 | filter1.on and cutoff act (two cascaded one-pole low passes). Missing: resonance, drive, mix, keytrack, model and style (the 80 models), blend, pan, level, the clean mode. |
| Filter 2, FX filter, model settings | 49 | 0 | 12 | 37 | A second filter, the filter models, the formant / comb / phaser settings, the serial / parallel switch, the filter of the effect chain. |
| Envelopes 1 .. 6 | 96 | 0 | 42 | 54 | The voice has a fixed envelope (5 ms attack, 150 ms decay, 0.7 sustain, 250 ms release); no envelope module, no curves, no beat units, no routing to the destinations. |
| LFOs 1 .. 10, random LFOs | 202 | 0 | 66 | 136 | No LFO of any kind: shapes, sync, rate, delay, fade, key follow, the custom shape editor, the random LFO styles. |
| Macros 1 .. 8 | 8 | 0 | 4 | 4 | The macro knobs are stored; nothing is routed from them (no modulation matrix). |
| Modulation matrix (64 slots) | 1024 | 0 | 576 | 448 | No modulation matrix: sources, destinations, amount, curve, stereo, aux sources, rise / fall / delay, per voice override. |
| FX chain: order, racks, slots (16 instances) | 480 | 0 | 471 | 9 | The effect chain, the 16 instance slots, the racks and the order. The own engine has no effect at all. |
| FX: chorus, flanger, phaser | 35 | 0 | 4 | 31 | The modulation effects. |
| FX: delay, reverb | 37 | 0 | 13 | 24 | Delay (sync, offset, filter) and the reverb algorithms. |
| FX: compressor, distortion, EQ | 54 | 0 | 13 | 41 | Dynamics, the distortion types and shaper curves, the EQ (bands, laws). |
| FX: hyper dimension, utility, Bode, convolve, splitters | 66 | 0 | 66 | 0 | The remaining effects and the band splitters. |
| **All** | **2637** | **7** | **1757** | **873** | |

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
