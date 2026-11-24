# VOICEGLB report (block B15: G12, G13, G14, G15, G16, G18)

Branch `work-voiceglb` (take-over of `agy/voiceglb`, which stopped after one commit cut from the old commit 629805b). Spec: `13-new-keys.md` 4.19, assumptions A100-A112, morning questions 28-30. Definitions follow the Serum 2 user guide (VOICING p.216-217, LFO RATE menu p.192, Setting the Quality p.319-320).

## Merge

`agy/voiceglb` merged into develop (B14, B16, B17, B18): its keys became the last block (positions 1576..1593, 18 keys with the new `master.bend_down_link`; destination choices 442..452, `kNumModDestinations` 453; cap bits 37..42; Kick pages 116 and 117, 118 in all). Generated files rebuilt with the repo tools (`params.json`, `PARAMETERS.md`, `kick-pages.json`, the 112 presets: only the 18 new keys at their defaults are added, `gen-factory-presets.py --check` and `check-params-doc.sh` pass).

## What the Gemini commit really did, and what was wrong

| Item | Found | Fix |
|------|-------|-----|
| G12 steal priority | mapped to Vital's `voice_priority` the wrong way round (Vital names the voice that is kept) and the default (0) was not the engine default (round robin), so every preset would have changed once the polyphony ran out; the test was `diff >= 0` (always true) | Serum 2 names and sense (Newest, Oldest, Highest, Lowest, Velocity), default = engine default; new `kVelocity` priority in the voice handler; test checks which note survives for all five |
| G12 same note | worked (Steal / Stack / Ignore) | kept; test: Stack doubles the level, Ignore is sample identical to a single note on, a slow attack shows the Steal retrigger |
| G13 poly legato | state in `static` globals shared by all six envelopes, all voices and all engine instances (the last envelope processed won, so env 1 could continue from env 6), and the stale level was never cleared (with the option on every later note had no attack) | one `PolyLegatoBus` per envelope and engine, only a held voice (attack, hold, decay) is recorded, published once per block; tests: new voice continues at the held level, an unrelated envelope is not mixed in (power ratio 1.001), a separate note after a legato phrase renders identical to the option off |
| G14 bend down | default 2 st changed every state, Vital import and preset with another `bend_range` | `master.bend_down_link` (On = version 2, one range both ways); tests with pitch measurements |
| G15 stereo spread | worked as mid/side width; dead `LowerBound` processor and a dead fallback | cleaned; test: side RMS scales 1 : 0.5 : 0, 0 % exactly mono, mid unchanged, macro modulation collapses it |
| G16 LFO swing | warped halves of the whole cycle (not a swing) | four pairs of slots per cycle, second slot delayed; test checks the level against the exact warp model |
| G18 no smoothing | global `static` flag (one instance changed all others) | `thread_local`, set before every block; test with two engines in a row |
| G18 4x | switched on the audio thread: reverb, delays and others reallocate their buffers (found: 1040 allocations in `parameter_sweep_every_parameter_min_default_max`) | read in `prepare ()`, `IEngine::needsPrepare ()`, JUCE shell prepares again from a timer with processing suspended, CLAP shell `request_restart`; sweep allocation free again; test: 1x > 2x > 4x aliasing of an FM patch |
| caps | all six bits set, tests weak | each bit is proven by an off/on measurement (`test_wp_voiceglb.cpp`) |
| Kick | keys only on a `more:` sheet | pages `voice_global`, `quality_options`; LFO swings on the `lfoN` more sheets |
| UI | key names added to existing panels | new `GlbVoicePanel` (4 primary, bend down behind More), quality options primary on `GlbQualityPanel`, swing on `LfoPanel`; test with assign-by-touch on stereo spread and `lfo1` / `lfo10` swing |

`state_random_walk` became silent with the larger table: seed 2026 -> 2027 (comment as before).

## Files

Engine: `core/engine-vital/upstream/src/synthesis/modulators/envelope.{h,cpp}` (bus), `modules/envelope_module.*`, `modules/synth_voice_handler.*`, `framework/voice_handler.{h,cpp}` (kVelocity), `modulators/synth_lfo.h` (swing), `utilities/smooth_value.h`, `synth_engine/sound_engine.*`, `common/synth_parameters.cpp`; adapter `vital_engine.cpp`, `vital_bindings.cpp`. Core: `param_table.cpp`, `engine_caps.*`, `modulation.h`, `kick_pages.*`, `engine.h` (`needsPrepare`). Shells: `PluginProcessor.*`, `synthyboy_clap.cpp`. UI: `GlbVoicePanel.cpp`, `GlbQualityPanel.cpp`, `MasterVoicingPanel.cpp`. Tests: `core/engine-vital/tests/test_wp_voiceglb.cpp`, `shell-juce/ui/tests/test_glb.cpp`, count/position tests of `core/tests`.

## Open questions

See morning questions 28 (swing division / global amount as in Serum 2), 29 (4x applied by prepare, ends sounding notes), 30 (bend down and stereo spread are our definitions).
