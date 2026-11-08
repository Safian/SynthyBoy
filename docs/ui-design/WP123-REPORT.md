# WP1, WP2, WP3 report - event contract, modulation core, voice / global (2026-11-08)

Branch `worktree-agent-a81cc04fd76139cb9`, based on develop `8c831d1`. Spec: `13-new-keys.md` section 11. Commits: WP1 `789d79e`; WP2 and WP3 are one commit (the changes to `vital_engine.cpp`, the voice handler and the connection processor are interleaved; they could not be split without interactive staging).

## Capability bits (a bit is set only with a test that renders the key off and on)

| Bit | engine-vital | engine-own | Test |
|-----|:---:|:---:|------|
| `kCapEventContract` | set | set | `core/tests/test_events.cpp` (sustain on / off / per channel / all channels, tuning expression by note id and by key + channel), `test_shell_clap.cpp` (CLAP MIDI CC 64, note expression, transport through the real shell) |
| `kCapMacros58` | set | - | `test_wp123.cpp: macros_5_to_8_are_sources_and_destinations` |
| `kCapLfo910` | set | - | `lfo_9_and_10_run_and_modulate` |
| `kCapModSourcesExtra` | set | - | `new_mod_sources_lift_slide_oct_note_poly_aftertouch` (and aftertouch vs poly aftertouch are different sources) |
| `kCapAuxSource` | set | - | `aux_source_multiplies_the_slot` (aux 0 / 50 / 100 %, invert, output 50 / 200 %, curve, aux alone does nothing) |
| `kCapLfoOptions` | set | - | `lfo_direction_reverse_and_ping_pong`, `lfo_mono_keeps_the_phase_across_notes`, `lfo_host_locks_the_phase_to_the_transport`, `random_lfo_rossler_attractor_style` |
| `kCapEnvBeats` | set | - | `envelope_beat_units_follow_the_host_tempo`, `envelope_legato_inverted_retriggers_on_legato_notes` |
| `kCapCurves` | set | - | `velocity_and_note_curves_and_their_legato_glide` |
| `kCapCustomLfoShape` | set | - | `custom_lfo_shapes_and_point_modulation`, `set_content_from_another_thread_while_the_engine_plays` |
| `kCapKeyScale` | set | - | `key_scale_conforms_notes_before_the_allocator` |
| `kCapTuning` | set | - | `tuning_concert_pitch_scales_user_file_and_mts_esp` (A4, Pythagorean, just, 24-EDO user file, broken file = 12-TET, `.kbm` unmapped key, MTS master +50 cents / no master / live follow / note-on-only) |
| `kCapMpe` | set | - | `mpe_per_note_bend_pressure_and_slide` |
| `kCapOscMapping` | set | - | `oscillator_mapping_gates_folds_and_keeps_modulating` |

Not set anywhere: `kCapSampleOsc`, `kCapUserWavetable`, `kCapWarp2`, `kCapMixerGraph`, `kCapFxRacks`, `kCapFilterExtras`, `kCapFilterModels`, `kCapFx*`, `kCapReverbForce` (WP4 to WP8). `capabilities ()` of the own engine is `kCapEventContract` only: it has no modulation matrix, tuning or scale; the sustain pedal and the per-note tuning act, pressure / brightness / release velocity are accepted and have no destination.

## What was added

* **WP1.** `Event::SustainPedal`, `Event::NoteExpression` (`Event::Expression` tuning / pressure / brightness), release velocity in `NoteOff::value`; `IEngine::setTransport`, `setContent`, `setTuningSource`; `core/include/synthyboy/midi_events.h` (one MIDI translation for both shells: CC 64, CC 74, poly / channel pressure, note on / off with release velocity, bend, CC 1, CC 120 / 123); CLAP shell: `CLAP_EVENT_MIDI` (the note port advertised the MIDI dialect without handling it), `CLAP_EVENT_NOTE_EXPRESSION`, transport, `setContent` after a state load; JUCE shell: translation through `midi_events.h`, `getPlayHead` transport, `setContent` after a state load. The Event names are CamelCase like the existing ones (`SustainPedal`, not `sustainPedal`).
* **WP2.** Everything of the table above, inside the Vital-derived engine (we own the fork): macros 5-8 and LFO 9-10 raised in the imported constants; `poly_aftertouch` output; aux input in the connection processor; LFO direction and host lock inputs; Rossler random LFO; envelope `legato_inverted`; envelope beat units through gated bindings; velocity / note curves as remap tables of the matrix slots with a legato glide; custom LFO shapes baked from `content.lfo_shapes` with `core/shapes`; the `content` hand-off (`IEngine::setContent`: built on the message thread, taken over by the audio thread through an atomic pointer, retired snapshots freed by the message thread only).
* **WP3.** Key / Scale note filter in the adapter; tuning (the tuned note of each key in the voice handler, 12-TET / Pythagorean / just / user `.scl` + `.kbm` through `core/tuning`, A4 as a slewed transposition offset, the MTS-ESP seam `ITuningSource`); MPE (manager and member channel bend ranges, member channel pressure = poly aftertouch, CC 74 = slide); oscillator mapping (per voice mute, octave fold, warp flag).
* Upstream changes are listed in `docs/UPSTREAM.md` (new rows after `producers_module.cpp`). The engine-vital README has the section "State version 3".

## Verification

* Version 2 behaviour unchanged: `version_2_sound_is_unchanged_in_both_engines` (default and busy patch, both engines, bit exact hash on the golden platform) passes. Two things had to change for that: the oscillator fold is an input of `SynthOscillator` (an intermediate `Add` processor changed the busy patch), and an LFO's sync clock stays 0 unless mono or host is on (as in version 2).
* RT allocation: `synthyboy_test_rt` passes, with a new test `process_does_not_allocate_with_the_new_events_content_and_keys` (all new events, transport, content swaps, an MTS-style source, random new keys, aux sources; both engines): 0 allocations in `process ()`.
* Gates, run once at the end: `cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=ON && cmake --build build -j8 && ctest --test-dir build`: build clean (VST3 + CLAP), ctest 29 / 29 passed. `build/clap_smoke build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap`: all checks passed. `scripts/check-params-doc.sh`: params.json / PARAMETERS.md up to date (no key changed). **clap-validator is not installed on this machine (`which clap-validator`: not found; README builds it from source with cargo in CI): not run.**
* Tests that had to change because the keys are live now: `engines_ignore_the_new_keys` (supportsParam follows the caps), `every_parameter_is_bound_or_special_exactly_once` and two siblings in `test_vital_params.cpp` (v3 keys bound / special iff their bit is set; the millisecond and beat key of an envelope time share a control by design), the v2 document test of `test_state_v3.cpp` (loaded over the defaults for engine-vital, over random v3 values for the own engine), `crosscheck_table_equals_adapter_bindings` (the Vital preset table covers the 730 version 2 bindings), `engines_report_only_known_capabilities_and_never_change_them`.

## Assumptions made here (add to 13-new-keys.md section 12)

* **A29.** Note expressions (tuning, pressure, brightness) are applied whatever `master.mpe.on` is (explicit data of the host); `master.mpe.on` decides what MIDI channel messages mean: member channel bend range, member channel pressure as poly aftertouch.
* **A30.** The aux mapping "bipolar source -> (x + 1) / 2" (A6) is the identity in engine-vital: all its sources are 0..1 at the output (the bipolar display is the slot flag).
* **A31.** Point modulation: y' = clamp (y + amount * s), x' = clamp (x + amount * s) between the neighbours (end points keep x); s is 0..1 for macros / mod wheel / channel pressure and -1..1 for the pitch wheel.
* **A32.** Envelope beat times use the tempo of the current block (a tempo change moves a running stage); not frozen at the stage start.
* **A33.** (Retrigger and Sync types; see A45 for the envelope and loop types.) `lfoN.mono` = the engine's Sync mode on the running engine clock (the phase is shared by all voices and continues across notes). It applies to the Retrigger and Sync trigger types; the envelope and loop types are note bound.
* **A34.** Velocity / note curves are remap tables of the matrix slots reading those sources; the amplitude's own velocity tracking is not curved. The legato glide smooths a source per voice over `master.portamento.time`; a new voice starts on its value.
* **A35.** Concert pitch: the key tables are anchored at A4 = 440 (key 69); `master.tuning.a4` is an offset added to the transposition control and slewed within 20 ms (it moves sounding notes). An MTS master overrides scale and A4.
* **A36.** The Rossler random LFO is a SIMD re-implementation inside the imported random LFO (the Vital library depends on nothing of ours) with the equations, constants, time scale and normalisation of `shapes::RosslerAttractor`; there is no trajectory-equality test against the package (different initial jitter per voice), the tests check that it drives a level, moves, stays finite and differs from the other styles.
* **A37.** `IEngine::setTransport` (bpm, song position in quarter notes, playing) is the transport contract; the engine defaults to 120 BPM, stopped.

## BLOCKED / not done (with the exact reason)

1. ~~**Point modulation of a custom LFO shape by per-voice sources**~~ **RESOLVED** (`LFOVOICE-REPORT.md`): every voice of an LFO bakes its own table from the sources of that voice, incrementally and within a per-tier budget; proved by `core/engine-vital/tests/test_lfovoice.cpp` (velocity and envelope state of two notes give different LFO output, deterministic, no allocation, incremental bake equals a full bake). `kCapCustomLfoShape` stays set. Assumption A31 now holds for every source.
2. ~~**`lfoN.mono` for envelope / loop trigger types**~~ **RESOLVED** (A45 in `13-new-keys.md`): one shared instance per note group; assumption A33 applies to the Retrigger and Sync types only.
3. **MTS-ESP client**: only the seam (`IEngine::setTuningSource`, `ITuningSource`) exists; no shell wraps `libMTSClient`, so no host-visible "no master" status yet. The licence of `libMTSClient` is recorded as "believed permissive, not verified" in `THIRD_PARTY.md` and must be checked before a shell links it.
4. **clap-validator** not run (not installed).
5. Own engine: nothing but the event contract (documented in `core/engine-own/README.md`).

## Differences between the engines

| Feature | engine-vital | engine-own |
|---------|--------------|------------|
| Sustain pedal, per-note tuning | yes | yes |
| Pressure, brightness, release velocity | sources (`poly_aftertouch`, `slide`, `lift`) | accepted, no destination |
| Macros 5-8, LFO 9-10, aux, LFO options, envelope beats / legato inverted, curves, custom shapes | yes | no (bits not set) |
| Key / Scale, tuning, MPE, oscillator mapping | yes | no (bits not set) |
