# GROUPA2 - WP-07 Voice control, WP-02 Preferences and dead toggles, WP-17 Live read-out

Branch `work-groupA2` (from `develop` 7094c23). Spec: `13-new-keys.md` 4.30 (WP-07, block B26), 4.31 (WP-02), 4.32 (WP-17); assumptions A369 .. A396; morning questions 71 .. 76. Keys 2063 -> **2125** (positions 2063..2124, all of B26), modulation sources 39 -> **42** (Voice Index, Voice Mod 1, Voice Mod 2), modulation destinations unchanged (908), capability bits **58 and 59** (two new), `kMaxParams` 4096 untouched, state version unchanged, Kick pages 135 -> **143**. `gen-factory-presets.py --check` passes (the 112 presets carry the 62 new keys at their neutral defaults; the renders are unchanged).

## 1. What was built

### WP-07 Voice control (B26)

What B14 / B15 already had (polyphony, steal priority, same-note, legato, poly legato, portamento, unison laws, oscillator mapping) is not built again; WP-07 is the per voice panel of Serum 2 (p.316-319) and its sources.

| Part | Files | One line |
|------|-------|----------|
| Keys, caps, Kick | `param_table.cpp` (B26, 62 keys), `modulation.cpp` / `.h` (3 sources), `engine_caps.*` (`kCapVoiceControl`, `kCapVoiceSources`), `kick_pages.cpp` (8 pages `voicectl_*`), regenerated `params.json`, `PARAMETERS.md`, `kick-pages.json`, the 112 presets | length 1, 8 steps x (pan, detune, cutoff, env time, mod 1, mod 2) 48, random 5, scaling 3, mask 5 |
| Engine | `voice_handler.cpp/.h` (`evaluateVoiceControl`, per voice outputs, note counter), `oscillator_module`, `sub_module`, `sample_module` (pan / detune adders), `producers_module`, `filter_module` / `filters_module` (cutoff), `envelope_module` (time factor), `lfo_module` / `random_lfo_module` (rate factor), `synth_voice_handler`, `sound_engine`, `synth_lfo.h` (48 point sources), `vital_engine.cpp` (`applyVoiceControl`, sources, caps), `vital_bindings.cpp` | a note takes the step `(counter - 1) mod length`, random parts are a hash of (seed, counter, stream), defaults add 0 and multiply by 1 |
| UI | `GlbVoiceControlPanel.h/.cpp` (step grid, row chips, length buttons, Init, Presets), `Panel.h/.cpp` (`excludeFromFlow`), `library_items.*` (`synthyboy-voice-control` item), `ContentIO.cpp` (a style goes onto the patch) | 62 keys = 62 controls (4 primary), 44 pt cells, two lines of four on a phone |
| Importers | `serum_import.cpp` (`voicePanel`), `serum_fx.cpp` / `serum_api.cpp` (`runStyle`), `serum_map.cpp` (sources 56 / 57 / 58), `serum_plugin_import.cpp`, CLI `<name>.voicecontrol.json`, `scripts/audit-serum-import.py` (`audit_voice_panel`, mutation of `voicectl.s1.pan`) | 18 `.SerumStyle` files import with 0 dropped; 52 presets carry a panel |
| Tests | `test_voicecontrol.cpp` (10 cases, 882 checks: caps, defaults bit exact, pan / mask / wrap, detune of oscillator, sub and noise sample, cutoff, envelope time, sources, LFO scaling both modes, randomness, chords), `test_new_keys` / `test_engine_caps` / `test_fxextra_keys` / `test_shell_clap` updated, `test_vital_params` (the keys are special), `test_user_content.cpp` (item), `test_serum_import.cpp` (2 cases), `test_ui_content.cpp` (panel) | |

### WP-02 Preferences and dead toggles (no keys)

| Part | Files | One line |
|------|-------|----------|
| Store | `preferences.h/.cpp`, `UiHost` (`preferences`, `setPreferences`, `modWheel`, `aboutText`, MIDI map and default map calls), `PluginProcessor.*` | 14 preferences in `<userDir>/preferences.json` |
| Dead toggles made real | `ParamControls.cpp` (double tap / value box, bubble, wheel), `Editor.cpp` (shortcuts), `OscWaveView.cpp`, `WavetablePanel.cpp` (2D / 3D), `ControlModel.*`, `UiContext.h`, `Sheet.cpp` (semitones), `ModModel.*` (limit depth), `WavetableEditorSheet.*` (mod wheel scan), `GlbSettingsPanel.h/.cpp` (14 toggles, About) | each one is read where it acts and has a test |
| Panic / flush on stop | `IEngine::silence`, `TransportStopWatch`, `vital_engine.cpp`, `PluginProcessor.cpp`, `synthyboy_clap.cpp` | both shells |
| Ultra render | `IEngine::setUltraRender`, JUCE `prepareToPlay` / `setNonRealtime`, CLAP `render` extension | 4x oversampling for an offline bounce |
| Locks | `shell_features.*` (`matrix`, `quality`, `master.tuning`), `ModMatrixPanel.cpp`, `GlbQualityPanel.cpp`, `GlbTuningPanel.cpp` | Lock Matrix, quality, tuning (were dead or missing) |
| MIDI maps | `preferences.cpp` (files, default map, Serum ids), `PluginProcessor.cpp` (new instance, Init, preset change, preference), `serum_import.cpp` (`midiMapSection`), CLI `.sbmidimap` | load / save / default map, `.SerumMIDIMap` and preset maps (macros) |
| Shortcuts | Appendix G set of 13-new-keys 4.31 | Cmd/Ctrl-click, Shift-drag, Shift-Alt-click, Alt-click on the filter display |
| Tests | `test_preferences.cpp` (5 cases), `test_silence.cpp` (3), `test_shell_clap.cpp` (+3), `test_serum_import.cpp` (+1), `test_ui_content.cpp` (+9) | |

### WP-17 Live read-out (no keys)

| Part | Files | One line |
|------|-------|----------|
| Engine | `readout.h` (`LiveValues`), `IEngine::setReadoutTargets / copyReadout`, `vital_engine.cpp` (`updateReadout`), `vital_bindings.*` (`invertParam`) | sources, LFO phases, envelope stages, the modulated value of up to 96 knobs, written at the end of every block |
| UI | `LiveBus.h/.cpp`, `UiContext::live`, `UiHost`, `ParamControls.*` + `Overlays.*` (knob mark), `LfoPanel.cpp` (playhead, trace), `EnvCurveView.*` (playhead), `OscWaveView.*` (live unison start), `GlbMpePanel.h/.cpp` (the pad sends and follows) | displays bind themselves and cost nothing hidden |
| Tests | `test_readout.cpp` (4 cases, 4483 checks incl. `invertParam` for every continuous binding), `test_ui_content.cpp` (+6) | |

**Scope decision.** "Marker animation shows only the range band" (WP-01) is in the read-out package and closed. "The MPE pad does not send events" (GROUPA1) is an input path, but the pad is also the display of the three axes, so the package took both (`GlbMpePanel.h:57` `sendExpression`); a note is not played by the pad (question 76).

## 2. Verification

* Build: incremental Release build after each package, **zero compiler warnings** (the linker's "ignoring duplicate libraries" notes are older). Build directory `build/` inside the worktree with `FETCHCONTENT_SOURCE_DIR_*` pointing at the sources of the main checkout.
* Per package, the affected suites: WP-07 `synthyboy_test_vital_voicecontrol`, keys / caps / params / clap / serum_import / ui_content (the first ctest also found two stale expectations: the Kick page count in `test_shell_clap` and the `isSpecialParam` list for `voicectl.*`; both fixed); WP-02 `preferences`, `vital_silence`, `shell_clap`, `serum_import`, `ui_content`, the whole UI set; WP-17 `vital_readout`, `rt` (no allocation on the audio thread), `ui_content`.
* Serum 2 import audit (`python3 -I scripts/audit-serum-import.py --serum ... --imported ... --selftest`): 656 files, 222,965 report entries, **0 errors**, all 8 mutations found (the new one: `voicectl.s1.pan`); new checks: 52 voice panels, 48 MIDI map entries; dropped leaves 886,903 -> 886,306 (voice panels and maps). The 18 `.SerumStyle` files and the `Macros CC14-21` map import with 0 dropped.
* `scripts/gen-factory-presets.py --check`: up to date (112 presets). `scripts/check-params-doc.sh`, `kick-pages.json`: regenerated with the repo tools. `ui-screenshots/` restored after the render tests.
* Full `ctest -j8` once at the end: see the last line of the final reply (recorded in `build/ctest-final.log`).

## 3. Assumptions and questions

A369 .. A396 in 13-new-keys.md 4.30 - 4.32; questions 71 .. 76 in 14-morning-questions.md. The ones that need the owner's ears or eyes: the value laws and scalings of the voice control (71), the Snap flag and the live evaluation (72), the preference defaults and the semitone format (73), the default MIDI map replacing the table (74), the LFO playhead of unrouted LFOs (75), the pad without a note (76).

## 4. Not done / limits

* Serum MIDI map ids of modules other than the macros are not decoded (the module numbers are not in the files we have); they are reported.
* The Serum "Snap" switch of the LFO scaling and the live evaluation of the voice control are my readings (questions 72).
* Noise and sub detune are tested with a content sample and a sine; the oscillators' detune does not apply with pitch tracking off (A376).
* An LFO or envelope that nothing uses has no playhead (the engine switches the module off, A393, question 75).
* The pad does not play a note; the Appendix G gestures for the clip player, arpeggiator, piano roll, preset browser, Option-F and the Renders folder do not exist in this synthesizer or belong to Kick (list in 13-new-keys 4.31).
* Hover audition of the Vital live display has no counterpart on a touch UI.
