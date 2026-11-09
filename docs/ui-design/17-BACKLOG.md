# 17 - Backlog: the remaining work as ordered work packages

Built from [16-COVERAGE-FINAL.md](16-COVERAGE-FINAL.md) (row ids such as S04-02 refer to it). State: `develop` bc376e4, 1594 keys, 43 capability bits, Kick pages 118. Two workers run on branches cut from develop and own their items (marked IN-PROGRESS in 16, not repeated here):

| Worker | Branch | Block | Items |
|--------|--------|-------|-------|
| warp-env (DONE on branch `work-warpenv`, WARPENV-REPORT.md; built as block B21 with the assumptions A270 .. A284, the lead renumbers at merge: the planned B21 of WP-04 below moves) | `agy/warpenv` | B19, assumptions from A220 | warp modes G34-G37 (Var fader, Bend +/-, Asym, Flip, Mirror, Remap 1-4, Odd/Even, Filter group, Distortion group, FM Thru-Zero / Exp / Linear, PD, AM) and the ENV 1 toggles G39 |
| mod-amount | `agy/modamt` | B20, assumptions from A240 | modulation of modulation amounts G1, per-connection curve G2, modulation of portamento / polyphony / stereo routing G3, source `stereo` G4, `stereo_routing` + `voice_override` G5, transpose quantize G6 of VITAL-IMPORT-AUDIT-REPORT.md |

## 1. Rules that apply to every package

* **Key budget.** `kMaxParams` = 2048 (core/include/synthyboy/params.h:77). 1594 used, 454 free. The packages below need about 175-210 keys; the two running workers are assumed to add at most 40. A 128-key RISE / FALL block (MANUALCHECK) would leave under 80 spare keys; WP-08 therefore stores RISE / FALL in the per-slot curve content (0 keys).
* **Key block rule** (13-new-keys.md section 1, COMMON RULES of the earlier worker texts): a new trailing block at the end of `kRaw[]` in `core/src/param_table.cpp`, ids = hash of the key, neutral defaults (the 112 factory presets must render identically; `scripts/gen-factory-presets.py --check`), appended choice values need a capability bit proven by an off / on render test (`core/tests/test_engine_caps.cpp`), regenerate `docs/params.json`, `docs/PARAMETERS.md`, `docs/kick-pages.json`, add Kick pages in `core/src/kick_pages.cpp` (the page test asserts every key is reachable), UI panel through `SYNTHYBOY_REGISTER_PANEL`, assign-by-touch for modulatable keys. Block numbers: B19 and B20 are taken; the packages below use B21 .. B28.
* **Merge cost.** Every key block touches the same files at the end of the table (`param_table.cpp`, `test_new_keys.cpp`, `golden_v2.h`, the generated files, the presets). "Parallel" below means different key blocks and different panels; the lead renumbers positions at merge exactly as for B14-B18.
* **Gates.** One incremental build and the affected suites at the end of each package; the full suite plus clean build once before the push (WP-20).
* **Evidence rule for this backlog.** A package is finished only when every item has engine, key, Kick page, UI and an audible / functional test, or is listed with the exact remainder.

## 2. Ordered work packages (summary)

| WP | Name | Rows closed (16) | Keys | Parts | Depends on | Parallel group |
|----|------|------------------|-----:|-------|-----------|----------------|
| WP-01 | Content pipeline in the shells | X01-01..03, S01-08, S02-03, S04-02..03, S04 snap / trim, S07-06 (UI), S10 Convolve IR, S13-01..17, S14, I01-16, S11 shape presets, S10 rack presets | 0 | shell + UI + tests; engine accessor for slice onsets | none | A |
| WP-02 | Preferences and dead toggles | S15-02..07, S15-13, S15-19, S11 Lock Matrix, S16 MIDI map files, S15-20, S16 shortcuts | 0 | shell + UI + tests | none (shares PluginProcessor.cpp / UiHost.h with WP-01) | A |
| WP-03 | The remaining 43 Serum filter types and the PZ SVF editor | S08 (5 rows), G57, G58 | 0 (choice entries) | engine mapping + UI picker + editor + tests | none | A |
| WP-04 | Noise, Sub, sample as a source (B21) | S07-02, S07-06 (engine), S07-08, I01-07 | 5 | engine + keys + Kick + UI + importer + tests | WP-01 for the file load UI | A |
| WP-05 | Oscillator extras (B22) | S01-20, S02 smooth interpolation, S04 single-slice modulation | 9 | engine + keys + Kick + UI + tests | (Swap Warps was built by warp-env) | B |
| WP-06 | Modulation sources and audio-rate taps | S11 (oscillator / filter outputs, Active Voices, Note-On Alt, Rand 2 / discrete, Release Velocity, Expression X, Fixed), S02 FM / PD / AM / RM from Noise, Sub, Filters | 0 (source and warp choices) | engine + choices + Kick + UI + tests | warp-env and mod-amount merged | B |
| WP-07 | Voice control (B23) | S15-08..11, S11 Voice Index / Voice Mod | about 62 | engine + keys + Kick + UI + tests | WP-01 for presets (optional) | A |
| WP-08 | Matrix RISE / FALL and control-sheet extras | S11 RISE / FALL, Aux submenu, G69 matrix part | 0 | engine + content + UI + tests | mod-amount merged | B |
| WP-09 | MPE completion (B24) | S15-21, S15-04 (MPE part), V01 | 4 | engine + keys + Kick + UI + tests | none | A |
| WP-10 | FX remainder (B25) | S10 delay left offset, reverb FEEDBACK, rack operations, rack presets, multi-instance (decision) | 2 | engine + keys + Kick + UI + tests | WP-01 for rack presets | A |
| WP-11 | Granular oscillator (B26) | S05 (7 rows), 02-24, 02-25 | 36-48 | new DSP package + keys + Kick + UI + tests | WP-01 (sample load); sample-osc package | C |
| WP-12 | Spectral oscillator (B27) | S06 (7 rows), 02-26 | 24-36 | new DSP package + keys + Kick + UI + tests | WP-01 | C |
| WP-13 | Multisample oscillator (B28) | S03 (5 rows) | 30-42 | instrument content + DSP + keys + Kick + UI + tests | WP-01 | C |
| WP-14 | Sample-class to wavetable | S04-last, S05-last, S06-last (G51) | 0 | conversion + UI + tests | WP-01, WP-11 / 12 for the Granular / Spectral entry points | D |
| WP-15 | Offline render: Resample, Render OSC Warp, share audio | S01-09, S13-16, S13-17, S15-05 (ultra render) | 0 | engine offline API + UI + tests | WP-01 | D |
| WP-16 | Wavetable frame <-> LFO shape | S11 copy row | 0 | UI + tests | WP-01 | D |
| WP-17 | Live modulation read-out | S11 live display, 14 row 10 | 0 | engine ring + UiHost + UI + tests | none | A |
| WP-18 | Importer follow-ups | I01-08..10, I01-15 | 0 (1 table choice) | importer + tests | mod-amount merged; WP-04 | B |
| WP-19 | Owner decisions | see section 4 | - | decisions only | - | - |
| WP-20 | Gates before the push | X01-09 | - | listening passes, clap-validator, full suite | all | last |

Sizes (relative): XL = a new DSP package or a cross-shell subsystem (WP-01, WP-11, WP-12, WP-13, WP-07); L = WP-03, WP-06, WP-15, WP-17; M = WP-02, WP-04, WP-05, WP-08, WP-09, WP-10, WP-14, WP-18; S = WP-16, WP-20.

## 3. Package details

### WP-01 Content pipeline in the shells (DONE on branch `work-wp01`, see WP01-REPORT.md; first; unblocks 10 other packages)

The audit's central finding (16 section 2, items 1-3): the engine handles user wavetables, samples and impulse responses (t_wp4 :179, :333, t_wp7 :231), but the shells never create or attach a content store, and the UI paths that look finished are facades.

Items:
a. `IContentStore` in `shell-juce` (PluginProcessor) and `shell-clap`: a user directory store (`<userDir>/content/{wavetables,samples,impulses}`, the user dir of core/src/shell_features.cpp) layered over the pack stores; `engine->setContentStore` on construction and on every state load; sha256 references via `ContentFileRef`.
b. Wavetable editor to engine: open from the oscillator's table (built-in frames or `content.wavetables[]`), `Save Table` writes a 16-bit WAV (the format of `UserWavetableStore`, tools/vital-import) into the store, adds the reference, sets `oscN.wavetable` = Preset table and calls `UiHost::setContent`; `Embed in Preset` = same into the preset pack; Export writes through a `FileChooser`; real Import (audio multi-cycle modes, single cycle, image, WAV) by `FileChooser` + `juce::AudioFormatManager`, replacing the generated test buffers (WavetableEditorSheet.cpp:1298-1330, :2228-2237, :2068-2069; WavetablePanel.cpp:709-721); Missing Files dialog on load.
c. Sample load for the Sample oscillator: file chooser, decode, root note from the file name (`Name F2.flac`, `Eb3`) and the WAV instrument chunk (S p.68), write into the store, `content.samples[]`; the real waveform in OscWaveView (replace the procedural drawing, OscWaveView.cpp:903-914); Auto slice markers (an `IEngine` accessor or shell-side onset detection with the same algorithm as `SampleData::detectOnsets`); Snap Off / Zero / Beats / Loop; Trim; marker animation under modulation.
d. Convolve `Load IR...` (FxConvolvePanel.cpp:26-31): file -> `content.impulses[0]`.
e. Vital import inside the plugin: `.vital`, `.vitalbank`, `.vitaltable`, `.vitallfo` through `core/vital-import` (link into the shell) with the report sheet; a `.lfoshape.json` loader into an LFO slot (14 row 25).
f. User library service on `UiHost` for the generic save / load of small JSON items: LFO shapes (S11 shape presets, favourites), formula presets and own formulas (S14), FX rack presets (S10), voice-control presets (WP-07).
g. CLAP / mobile: file access has no host UI in CLAP. Decision Q39 (CLAP custom extension vs linking `synthyboy_core` on mobile) is part of this package; default: implement a small CLAP extension `synthyboy.content` (import by path, list, remove) and use it from the Kick side; JUCE shell uses file choosers.
Keys: 0. Kick: none (content is not a key). Tests: headless with `MemoryContentStore` / a temp directory: save table -> reload state -> audio differs from the saw fallback; sample load -> `sample_oscillator_type_plays_the_content_sample_at_the_key_pitch` through the shell; Convolve IR -> tail appears; import of the synthetic fixtures of `tests/` (the 35 real presets stay out of git).
Files touched: `shell-juce/PluginProcessor.*`, `UiHost.h`, `WavetableEditorSheet.cpp`, `WavetablePanel.cpp`, `OscWaveView.cpp`, `FxConvolvePanel.cpp`, `shell-clap/`, `core/vital-import` link. Conflicts: WP-02 (PluginProcessor.cpp, UiHost.h), WP-11/12/13 (OscWaveView.cpp) - run WP-01 first or agree the UiHost additions up front.

### WP-02 Preferences and dead toggles

Items: a preferences store (JSON in the user dir) and the real behaviour behind: double-click reset / value box (S15-02), parameter value bubble toggle, mouse-wheel control on / off, keyboard shortcuts on / off, default waveform view 2D / 3D, frequency units Hz / semitones in every cutoff-type readout (S15-07, a formatter in `ParamControls`), MPE enabled by default, limit mod depth on drop (`AssignFlow` clamps the new depth to the headroom of the knob), mod wheel -> WT POS while the editor is open (needs WP-01 b), silence notes and FX tails when the host stops (S15-06: `IEngine` panic + effect flush from the transport-stop callback in both shells), load MIDI map from presets, ultra quality offline render flag (consumed by WP-15), real About line (version and build from CMake, S15-20). Lock Matrix (S11): make `matrixLocked` real by locking `modNN.*` + the curve content in `applyPatch`/`keepLockedContent` (core/src/shell_features.cpp); lock for `quality.*` and `master.tuning.*` (S15-13, S15-19). MIDI map load / save files and a default map (S16). Desktop shortcut set of Appendix G where it makes sense (S16).
Keys 0. Tests: the shf_* pattern (`ui_shellfeat`), `ct_shell_features`. Kick: none.

### WP-03 The remaining 43 Serum filter types, PZ SVF editor

Items: model / style slots for the 43 types that have DSP in core/filters-serum but no slot: Normal 12 (MG Low 12/24, Low 12/24, High 12/24, Band 12/24, Peak 12/24, Notch 12/24), Multi morphing 4 (LBH, LPH, LNH, BPN), Flanges 9 (Cmb / Flg / Phs L / H / HL), Misc 12 (Low / Band / High EQ 12, German LP, Formant I / II / III, Bandreject, Dist.Comb 1/2 LP/BP), New 6 (MG Ladder, Acid Ladder, MG Dirty, Comb 2, Exp MM, Exp BPF). Append models 22.. to `kFilterModels` (PT:49) with up to 12 styles each (`kModels` in `serum_filter_bank.cpp:27`, count constant 14 -> 14 + n), one capability bit (`kCapFilterModels2`), `fxfilter.model` shares the list. UI: hierarchical type menu with the manual's category / name structure (FilterModels.h), `Var` label per type. PZ SVF curve editor (`content.filter_curves`, UI drag points on the response view) with a test that the drawn curve moves the rendered response (reuse t_wp6 :262). Names final after the A/B listening (Q13).
Keys 0 (choice entries). Tests: like t_wp6 :189 (finite, bounded < 4, different from Analog and from each other, stable at extreme settings, all three filter slots). Parallel: yes (no other package touches `kFilterModels`).

### WP-04 Noise, Sub, sample as a source (block B21)

Keys (5): `sub.phase`, `sub.phase_contiguous`, `noise.stereo`, `noise.filter` (MANUALCHECK 4 specs), `noise.type` (Noise / Sample; the noise source plays `content.samples[]` through `SampleSource` - this is also the Vital sampler, audit G7 / I01-07). Engine: noise sample player with the existing `noise.loop`, `.phase`, `.pitch`, `.fine`, `.keytrack`; STEREO and FILTER act on the colour samples only. Importer: Vital `sample_*` -> `noise.type` = Sample (no free oscillator needed any more), `sample_bounce` -> a loop mode (extend `noise.loop` or map to forward / reverse), warp 'from sample'. Kick: `sub_main` and `noise` more sheets. UI: NoisePanel (sample name from the file, load through WP-01 c), SubPanel. Tests: off / on renders per key as t_oscextra; importer test on the 5 presets that lost the sample (Primal Hunger, Gentle Driven EP, Cloudgazer, Edge of Space, Time Spiral) with a synthetic fixture.

### WP-05 Oscillator extras (block B22)

Keys (9): `oscN.ratio_src` (3; choice Base note, Osc 1-3, Sub; own number excluded in the UI), `oscN.smooth_interp` (3; the `osc_N_smooth_interpolation` control, VE:856, replaces the tier-only decision), `oscN.sample.slice` (3; modulatable single slice for the Play Single Slice mode). Swap Warps (no key: exchange the 4 + 4 warp keys, UI action on the warp row). Hi-resolution wavetable (V p.40) stays tier-controlled unless the owner wants a switch (WP-19).
Swap Warps is done (warp-env: an entry of the warp lists, no key). Tests: ratio tunes osc 2 to osc 1 x ratio (pitch measurement as t_pitchuni :88); smooth interpolation off / on at a modulated WT POS; slice modulation moves the played slice (t_wp4 :295 pattern).

### WP-06 Modulation sources and audio-rate taps

Sources to append to `modSources` (docs/params.json): Oscillator A, B, C, Noise, Sub, Filter 1, Filter 2 (output of the module after level / pan, per voice, control-rate; the audio-rate FM / PD / AM / RM from them is the warp part below), Active Voices, Note-On Alt 1, Alt 2, Note-On Rand 2, Rand (Discrete), Release Velocity (note-off velocity is already an event field), Expression X (per-note bend), Fixed. Voice Index and Voice Mod 1 / 2 come with WP-07. Warp part: append FM / PD / AM / RM entries for the sources Noise, Sub, Filter 1, Filter 2 to the warp lists (after warp-env's entries; `legacyChoiceMax` in core/vital-import must keep capping imported Vital choices to the pre-append range).
Keys 0; `kNumModSources` 38 -> about 51, capability bit(s), loop check for sources that depend on the destination's own oscillator (as the aux-source loop check in LFOVOICE-REPORT section 1). Kick: source choices are on the `modNN` pages already. Tests: each source moves a destination (t_wp123 :204 pattern); FM from Sub changes the spectrum.
Depends on warp-env (warp lists) and mod-amount (source / destination tables, `stereo` source).

### WP-07 Voice control (block B23)

Serum 2 p.316-319. Keys (about 62): sequence length (1), 8 steps x 6 per-voice values (PAN, DETUNE, CUTOFF, ENV offset, MOD 1, MOD 2) = 48, randomization PAN / DETUNE / CUTOFF / ENVS (4), scaling of all envelope times and LFO times (3 incl. percent / rate mode), oscillator mask A / B / C (3). Engine: per-voice variation applied at note on in the voice handler (voice index = note counter modulo length), random draws from the voice's own seed (deterministic for the tests); sources Voice Index and Voice Mod 1 / 2 (WP-06 list). UI: `GlbVoiceControlPanel` (step grid, touch sized). Presets through WP-01 f. Kick: about 8 pages. Tests: each value off / on; 8-step sequence audible; determinism.

### WP-08 Matrix RISE / FALL and sheet extras

After mod-amount merges its per-slot curve content (`content.mod_curves[]`): add `rise` / `fall` (ms) to the curve record (0 keys; defaults 0 = today's sound), a slew limiter on the source in the curve stage (per voice for voice sources), RISE / FALL knobs in ModCurvePanel, Aux Source submenu and Edit Custom Curve on the control sheet (Sheet.cpp). If the owner prefers modulatable keys, the MANUALCHECK spec is 128 keys `modNN.rise` / `.fall` (budget in section 1). Tests: step response of the slew (rise 100 ms reaches 90 % in the measured time), defaults bit identical.

### WP-09 MPE completion (block B24)

Keys (4): `master.mpe.xyz_macro` (Off, XYZ -> Macro 1 / 2 / 3, YZ -> Macro 1 / 2), `master.mpe.y_modwheel`, `master.mpe.bend_to_x`, `master.mpe.y_bipolar`. Engine: mapping in the event path (t_wp123 :816 shows the current per-note bend / pressure / slide); VST3 / CLAP note expression stays available with MPE off (S p.325). UI: GlbMpePanel. Preference "MPE on by default" from WP-02. Tests: expression events drive macro 1-3 / mod wheel.

### WP-10 FX remainder (block B25)

Keys (2): `delay.offset_left`, `reverb.feedback` (specs in MANUALCHECK-REPORT section 4). Rack operations: cut / copy / paste / clear a bus, rack presets (WP-01 f); multi-instance modules only if the owner reverses A19 (WP-19). Tests: t_fxextra pattern.

### WP-11 / WP-12 / WP-13 Granular, Spectral, Multisample (blocks B26, B27, B28)

Each: a new DSP package under `core/` (granular, spectral STFT resynthesis, multisample zones with envelope), a new `oscN.type` entry (append after Sample; capability bit proven by an off / on render), per-oscillator keys, panel in OscWaveView, Kick pages, tests. They reuse the sample region keys (`oscN.sample.start/end/loop_*`, `scan`, `uni_*`) wherever the manual says the operations are shared (S p.71, 88, 108), so only the type-specific keys are new.
* Granular (36-48 keys over 3 oscillators): window amount / skew / shape, density, length, grain randomization, spawn pattern, X|Y control, pitch-preserving scan (the varispeed scan of S04 is the reason this oscillator exists).
* Spectral (24-36): high / low frequency markers, cut, filter curve (content), mix, X|Y, spectral warp mode.
* Multisample (30-42): instrument content kind (SFZ -> `content.instruments[]`, ContentPack), OVERRIDE / DELAY / A / H / D / S / R, vel track, rand, timbre; "switch the last note played to a sample".
Dependencies: WP-01 (load). Parallel: the three own different core packages and key blocks; recommended order granular -> spectral -> multisample so that each review sees one DSP family.

### WP-14 Sample-class to wavetable

"Switch to wavetable" for Sample (and Granular / Spectral once they exist): convert the start..end region with `wtedit::importAudioSamples` (core/wavetable-edit/src/audio_import.cpp) into a table, store through WP-01, set `oscN.type` = Wavetable and `oscN.wavetable` = Preset table. 0 keys.

### WP-15 Offline render

An `IEngine` offline render entry (render N frames of a note / one bar at ultra quality without the real-time caps), used for Resample to wavetable (S p.324), Render OSC Warp (256 frames across WARP; the same renderer feeds `wtedit`), and share / drag-out of the last note as audio (S01-09; mobile: share sheet). tools/preset-render already renders offline - reuse its path. 0 keys.

### WP-16 Wavetable frame <-> LFO shape

Frame -> LFO point list (resample 2048 -> points) and LFO shape -> table frame (S p.196-198). Needs WP-01 for the table write-back. 0 keys.

### WP-17 Live modulation read-out

Engine -> UI ring of modulation values (per source, plus per destination for the shown knobs), written in `processBlock` like `copyScope`; UI: live value line on modulated knobs, LFO / envelope playheads, random-LFO history (14 row 10). Capability bit not needed (UI read-only). 0 keys.

### WP-18 Importer follow-ups

Map `detune_range` -> `oscN.unison.range`, `midi_track` -> `oscN.pitch_track`, `voice_priority` -> `master.voice.steal_priority`, and the sample (after WP-04) in core/vital-import (vital_import.cpp:188); append the built-in `Saw down` (Vital's Init waveform) to the table list and map the 36 + 3 single-frame uses (audit G9); decide G8 (stored mod wheel) by an A/B; A/B listening of the clean-room renderer against real Vital (needs the owner's Vital; 14 row 1, Q34). Re-run `scripts/audit-vital-import.py` (0 mismatches, 0 unclassified).

### WP-19 Owner decisions (no work until answered)

1. iOS engine (14 row 11): does iOS ship the Vital-derived engine (licence / size) or must engine-own get the Serum-class features (it sets 1 of 43 capability bits)?
2. Global swing: one amount + division as in Serum (S p.192, 269) or keep per-LFO amounts (14 row 28). Cost if yes: 2 keys + UI.
3. Several instances of one effect and arbitrary module count (A19, 14 row 3).
4. MTS-ESP: verify the licence of libMTSClient and link it (14 row 5).
5. Serum 1 presets / Compatibility Mode (S15-14) - proposed: not built.
6. Alignments that were kept different (14 row 31): compressor limit attack / makeup / threshold range, phaser rate range, reverb default type, routing defaults for B / C / noise, unison blend default, distortion default. Each one changes the sound of the 112 factory presets.
7. Hi-resolution wavetable as a switch (V p.40), Round Robin as a separate priority.
8. Save-preset dialog and metadata editing: confirmed as Kick's (S01-07)?
9. MPC: screen resolution, hardware controls, page cycler (Q5, Q30) - no MPC shell exists in the repository.

### WP-20 Gates before the push

Listening passes that only the owner can do (14 rows 13, 15, 16, 21, 27, 31 and the filter / reverb names), clap-validator on the final tree (14 row 6), compiler-warning cleanup (14 row 7), full suite + clean build once, ASan / UBSan / TSan for the packages that touch audio buffers (WP-03, WP-06, WP-07, WP-11-13, WP-15).

## 4. Parallel plan

Different key blocks and different files can run at the same time; each group below is safe within itself (no two packages share a key block or a panel), groups are ordered by dependency.

| Group | Packages | Why it is safe | Start |
|-------|----------|----------------|-------|
| A | WP-01, WP-03, WP-04, WP-07, WP-09, WP-10, WP-17 (+ WP-02 after WP-01's UiHost/PluginProcessor additions are agreed) | WP-03 and WP-01 add no keys; WP-04 / 07 / 09 / 10 own B21 / B23 / B24 / B25 and different panels (Noise+Sub / new Voice panel / Mpe / Fx*); WP-17 adds a read-only ring | now (the two running workers do not touch these files) |
| B | WP-05, WP-06, WP-08, WP-18 | all read or extend what warp-env (warp lists, Swap Warps UI) and mod-amount (source / destination tables, curve content, importer) produce | after both branches are merged |
| C | WP-11, WP-12, WP-13 | own core packages and key blocks B26 / B27 / B28; share OscWaveView.cpp and the `oscN.type` list (append in order) | after WP-01; engine work can start earlier on a test content store |
| D | WP-14, WP-15, WP-16 | small, all need WP-01 (write-back of tables) | after WP-01 (WP-14 also after the sample-class oscillators it converts) |
| last | WP-19 answers, WP-20 | | |

One Claude fan-out at a time per the model policy; Gemini `agy` workers may take the groups A items that are DSP-only (WP-03 engine part) with their own worktrees.

## 5. Key estimate by block

| Block | Package | Keys |
|-------|---------|-----:|
| B19 | warp-env (done as B21 of its branch: 7 keys, 1596..1602) | 7 |
| B20 | mod-amount (running) | assumed <= 20 |
| B21 | WP-04 | 5 |
| B22 | WP-05 | 9 |
| B23 | WP-07 | about 62 |
| B24 | WP-09 | 4 |
| B25 | WP-10 | 2 |
| B26 | WP-11 | 36-48 |
| B27 | WP-12 | 24-36 |
| B28 | WP-13 | 30-42 |
| - | WP-01, 02, 03, 06, 08, 14-18 | 0 |
| | Total new | 172-208 (plus the 40 assumed) -> 1806-1842 of 2048 |

## 6. The ten biggest missing items

Ordered by effect on a user of the plug-in, not by size:

1. **Content pipeline** (WP-01): no way to load a sample, a wavetable, an impulse response or a Vital preset in the plug-in; the wavetable editor, Save / Embed / Export / Import and Convolve `Load IR...` do nothing real.
2. **Wavetable editor to engine link** (WP-01 b): the biggest UI that cannot change the sound.
3. **Granular oscillator** (WP-11).
4. **Spectral oscillator** (WP-12).
5. **Multisample oscillator** (WP-13).
6. **Voice control** (WP-07, with Voice Index / Voice Mod sources).
7. **43 Serum filter types without a slot** and the PZ SVF editor (WP-03).
8. **Oscillator / filter outputs and the other matrix-only sources, and FM / PD / AM / RM from Noise, Sub and Filters** (WP-06).
9. **Preferences and the dead toggles, Lock Matrix** (WP-02).
10. **Offline render features: Resample, Render OSC Warp, share audio** (WP-15).

Smaller but audible: MPE mappings (WP-09), RISE / FALL (WP-08), live modulation display (WP-17), the 12 MANUALCHECK keys (WP-04, 05, 10).


### WP-21 Serum 2 preset import (owner decision 2026-11-09, replaces the Serum 1 compatibility idea)

Goal: import `.SerumPreset` files (Serum 2) into our parameter model with the same discipline as the Vital importer: a `core/serum-import` package (no I/O in the library), a CLI in `tools/`, a per-file report (imported / approximated / dropped / ignored, nothing unaccounted), the independent audit script pattern of `scripts/audit-vital-import.py`, and tests on the 656 real presets installed at `/Library/Audio/Presets/Xfer Records/Serum 2 Presets` (copy to the gitignored `tests/serum-presets/`, never commit). First step: reverse the container (header, compression, JSON/CBOR body) from the real files and write the format down in `docs/ui-design/19-serum-preset-format.md`; then map every Serum 2 parameter to our keys or content (wavetables, samples, noise, LFO shapes, mod matrix with curves, FX racks); dropped items become backlog rows. Depends on: most feature packages (the more features exist, the less is dropped); the importer can start now and gain coverage as WP-01..13 land. Keys: 0 to a few. Size XL.

### WP-21 leftovers: what the Serum 2 importer drops (bulk run on the 656 real presets + 736 other files, WP21-REPORT.md)

Each row is a class of values the importer reads, accounts and drops because the model has no home for it. "files" = presets (or files of the family) that hold it. Rows named in the earlier lists (7: PZ filter, 8: oscillator / filter outputs as sources and FM / PD / AM sources, 5: multisample, 6: voice control) are the same work; the importer picks the data up the day those packages land.

| Row | Dropped (as `detail` in the report) | files | What it needs |
|----:|--------------------------------------|------:|---------------|
| S1 | Clip player: `MidiClip0..11`, `ClipPlayer*` (notes, automation lanes, grid state; also the 137 `.XferClip`, 14 `.XferClipBank` files): `clip_player_not_modelled` | 649 | a clip player / MIDI clip engine with a note + automation lane model (Serum 2 manual p.250 ff.) |
| S2 | Arpeggiator: `Arp0`, `ArpClip0..11` (shape, offset, repeats, gate, retrigger, velocity decay, transpose, wrap; 12 `.XferArp`, 4 `.XferArpBank`): `arpeggiator_not_modelled` | 219 | an arpeggiator with the Serum 2 pattern clips |
| S3 | Voice panel (`VoicePanel0`: eight step values for pan / detune / env time / cutoff / mod 1 / mod 2, random amounts, scaling; 18 `.SerumStyle`): `voice_panel_not_modelled`; the sources Voice Mod 1 / 2, Voice Index, Active Voices | 244 | WP-07 voice control |
| S4 | PZ SVF poles and zeros (`VoiceFilter#/PZs`, `FXFilter/PZs`; 31 `.XferFilterPZ*`): `pz_svf_poles_and_zeros_not_imported`; the filter type itself is imported, the response curve is not | 21 | a conversion from the Serum poles / zeros (frequency, Q, gain, type per pair) to the drawn `filter_curves`, or a real pole / zero filter |
| S5 | Path LFOs (`pathData`, 21 `.XferPath`) and the sources LFO 1..10 Y: `path_lfo_not_modelled` | 39 | an XY path LFO with two outputs |
| S6 | Multisample details: the other zones (reduced to one, A341), envelope override (`kParamEnv*`), timbre shift, velocity tracking, random phase of a zone; Granular (`kParamDensity`, grain length, window, randomisation) and Spectral oscillator parameters (`kParamSpecFlt*`, freq lo / hi, the spectral warps, `flex`): `multisample_feature_not_modelled`, `granular_feature_not_modelled`, `spectral_feature_not_modelled` | 493 + 99 | WP-12 / WP-13 |
| S7 | Noise: the sample file of the noise oscillator and `embeddedNoiseData` (Serum plays a file, ours has generators), `kParamColor`: `noise_sample_file_not_representable` | 482 | a noise source from a user sample (question 51) and a colour filter |
| S8 | Matrix: modulation of other mod slots' smooth / delay (`ModSlot` destinations), the slew (rise / fall) and the delay of a slot, the drawn aux curve (`aux_curve_not_modelled`), the output curve display; sources Alt. 1 / 2, Expr X, Fixed (`modulation_source_not_modelled`) | 152 | matrix extensions (G-series) |
| S9 | Destinations without a key: `Arp` / `MidiClip` parameters, `NoiseOsc.kParamColor`, `MultiSampleOsc` timbre shift, granular / spectral parameters, `Oscillator.kParamHzOffset`, `kParamPosition` of the sample player, compressor `Gain0..2`, `Ratio0..2` per band, delay `OffsetR`, bode shift sign, reverb `VintageScale`: `destination_not_modelled`, `effect_parameter_not_modelled` | 137 + 74 | per feature |
| S10 | Effects: the splitter bands (the modules after a splitter are flattened, A338; the pool has no band racks), the second and further impulse response (`only_one_impulse_response_per_patch`), per module options (`FiltMode` of the chorus, `NumPoles` of the phaser, `Depth2`, `BW` and `NumStages` of the distortion, `DimELevelOut`, `Link`, `SwapAB`), `effect_detail_not_modelled` | 166 | pool band racks; several IR slots |
| S11 | Global: swing (`kParamSwing`), S1 compatibility, the tuning table (`tuningData`, 2 files), `mpeConfig`, the scale quantizer details (the 12 note flags), the MIDI CC map (`.SerumMIDIMap`, `midiMap`), note latch, `kParamVoiceAmp` as a destination only | 656 | per feature |
| S12 | The sample player: `trimHead` / `trimTail` / `gain`, `kParamHzOffset`, `kParamPosition`, BPM scan range, base tempo, the loop start link | 90 | sample player extensions |
| S13 | The LFO point modulation target "curve" (target 2): `point_curve_modulation_not_modelled`; LFO swing flag; random LFOs beyond the fourth (`more_than_four_random_lfos`) | 57 | a curve source in `ContentPoint`; more Random LFOs |
