# WP4 to WP8 report - content store, sample oscillator, mixer / racks, filters, new FX, reverb force (2026-11-08)

Spec: `13-new-keys.md` section 11. Engine: `core/engine-vital` (our AGPL fork, every upstream change is listed in `docs/UPSTREAM.md`). A capability bit (`engine_caps.h`) is set only with a test that renders the key off and on and asserts a measurable difference. engine-own (iOS) sets none of the bits of WP4 to WP8.

## WP4 - content store, sample oscillator, user wavetable, warp slot 2

Bits set: `kCapSampleOsc`, `kCapUserWavetable`, `kCapWarp2`. Tests: `core/engine-vital/tests/test_wp4.cpp` (8 cases), `process_does_not_allocate_with_the_new_events_content_and_keys` (now also runs the sample oscillator, the user tables and content replacement under the counting `operator new`).

### Done

* **ContentStore** (`core/include/synthyboy/content_store.h`, `core/src/content_store.cpp`): `IContentStore::readFile (ContentFileRef, bytes)` (checks the SHA-256 when the reference has one), `MemoryContentStore`, `DirectoryContentStore` (`<root>/<pack>/<file>`). `IEngine::setContentStore ()` (message thread). `setContent ()` / `setContentStore ()` decode the referenced files on the message thread into the content snapshot, which the audio thread takes over without a lock (existing WP2 mechanism). The replaced snapshot is retired (may be freed) only after `applyParams ()` has repointed the wavetables and sample oscillators at the new one.
* **User wavetable (`oscN.wavetable` = Preset table, C1)**: a WAV whose length is a multiple of 2048 is that many frames (at most 256), anything else is one cycle resampled to 2048. Built into a Vital `Wavetable` (`vitaltables::buildUserWavetable`) and pointed at with `useExternalData` between blocks (no allocation). `oscN.frame` / `frame_spread` scale with the frame count of the table (`Binding::frameOsc`). Missing content: the saw, as before.
* **Sample oscillator (`oscN.type` = Sample)**: `sample_oscillator.h/.cpp` in the fork, rendering with `core/sample-osc` (`SampleVoice`), inside the voice (same note, transpose, tune, unison, level, pan, phase inputs and the same raw / levelled outputs as the wavetable oscillator, so filters, destinations and the mixer are unchanged). `core/sample-osc` was extended (all backwards compatible, its own tests pass): `LoopMode::tailed`, `setSemitoneOffset`, `setSpeedMultiplier` (signed), `releaseLoop`, `processModulated`, `seekFraction`, `edgeFadeMs`, `SampleData::normalizeGain`, `detectOnsets` / `detectSlices`.
  * start / end, root, key tracking, the five loop modes, loop start / end, loop crossfade (ms), reverse, fade edges (1 .. 128 ms, linear at both ends of the region), normalize (peak of the whole file), unison start / span (random / evenly spread start offsets), `phase` and `random_phase` as the start position.
  * **Slices**: Auto = deterministic onset detection of the file (2 ms RMS envelope, rise against the mean of the previous 20 ms, minimum distance 20 ms, no onset within 20 ms of the file start) filtered by `slice_threshold`; Manual = `content.samples[].slices`. The key picks the slice (`key - slice_root`, clamped), the pitch is the recorded pitch (+ transpose + tune), `slice_play` = to the end of the region or the single slice.
  * **Scan** (A13): speed = `scan` x range (x1, x2, x4, x8), sign flipped by `scan_reverse`, x host tempo / 120 with `scan_lock_tempo`, `scan_to_bpm` makes the region last `length_beats` at the host tempo. The speed multiplies the pitch ratio: it is varispeed (pitch follows speed). Pitch-preserving time stretching would be the granular oscillator (Q6: stays GAP). `scan = 0` freezes the read head.
  * `loop_exit` and Tailed: on key release the loop is left and the sample plays on to the end of the region.
  * Unison: voices 1 .. 16 as for the wavetable oscillator (detune range x detune, detune power, stereo spread, blend, equal-power normalisation); the quality tier caps the voices.
* **Warp on a sample (decision, section 4.9)**: a sample has no cycle, so the phase-domain warps (Sync, Formant, Bend, Squeeze, Pulse Width) have nothing to bend and are inert on a sample voice. **Quantize** reduces the resolution of the output (2^(1 + 14 (1 - amount)^3) levels), **FM** (osc A, osc B) modulates the read speed of the voice with the source, **RM** (osc A, osc B) multiplies the output with the source. Both slots work this way. (Wavetable oscillators: all 13 types in both slots.)
* **Warp slot 2** (`oscN.warp2.*`): runs on the phase slot 1 produced (A15), all 13 types, skipped completely when None (the sound of a patch with slot 2 off is bit-identical to before: golden test). An oscillator waits for the FM / RM sources of both slots.
* **Quality tiers**: sample interpolation 4-tap (tier low: oversampling 1), 8-tap (medium), 16-tap with mip blending (high); `maxUnison` caps the sample unison too. No module is bypassed.
* **engine-own (iOS)**: none of the bits.

### Differences / limits (not hidden)

* `loop_link` is an editor action (keep the loop length while dragging the start): no DSP effect, by specification (13-new-keys.md A39). The key is stored.
* `loop_relative` On: loop start / end are fractions of the playback region instead of the file.
* A change of `oscN.type` while a note sounds is crossfaded over 10 ms (FIX1; the sample path itself starts from the next note).
* Normalize uses the peak of the whole file, not of the start..end region.
* The wavetable file format is WAV (the `.sbwt` of C4 does not exist in the repository).
* The Vital import (11 Annex) still maps nothing to `oscN.type` (WP9).

## WP5 - mixer graph and racks

Bits set: `kCapMixerGraph`, `kCapFxRacks`. Tests: `core/engine-vital/tests/test_wp5.cpp` (9 cases: filter blend crossfade, None as modulation source, sends and buses and Other bus and Direct, filter destinations, racks, per-rack order, 16 slot repair, random settings), plus the random key runs of `test_rt_alloc` (0 allocations).

* **Sources**: `oscN.destination` / `noise.destination` 5 = Filter blend (equal power between the filter inputs; a switched off filter lets its share through to the effects, as for the engine's own filter destinations), 6 = None (no audio, still a warp source). `oscN.filter_blend`, `oscN.busM_send`, `noise.*` are poly modulation destinations ramped over the block; a send taps the source after level and pan whatever the destination is.
* **Filters**: `filterN.destination` (Effects only / Direct out / None; in series the filter that feeds the other has no destination of its own) and `filterN.busM_send` (tap after the filter's mix).
* **Buses**: sends -> (Other bus) -> rack -> `busN.level` -> Main / Direct / Other bus (both on Other: bus 2 goes to Main; bus 2 runs first when it feeds bus 1), `busN.fx_bypass`. Main = voices + buses to Main -> Main rack (`main.fx_bypass`) -> master volume.
* **DIRECT**: skips every rack, scaled by `direct.level`, added after the racks. **Deviation from A16**: it still passes `master.volume`. In version 2 the engine's Direct out is summed before the volume, and R5 (a version 2 patch with an oscillator at Direct out must sound the same) wins over the assumption that Direct skips the master volume.
* **Racks**: `<fx>.rack` for the 16 modules (Main, Bus 1, Bus 2, 7 band racks). `ReorderableEffectChain` was rewritten: each effect processes the signal of its own rack, in the order of `fx.order.1..16` filtered to that rack (per-effect segment processing of the Vital effects: every module is called on its own); `BandSplitterModule` is the interface the splitters of WP7 implement (split, run the band racks, merge). The modules 9..15 are pass-through placeholders in WP5.
* **`fx.order` repair** for 16: `resolveFxOrder (choices, order, 16)` (a later position takes the lowest module not yet placed; tested). The float encoded `effect_chain_order` (9! fits a float, 16! does not) is replaced by 16 controls; the Vital import still decodes the old code in `core/vital-import`.
* **Version 2 sound**: the default patch and the busy patch of `test_new_keys` render bit-identically (the effects run in the same order on the same signal; a silent bus costs one scan of the block).
* **Tier low / medium**: no module is bypassed by the mixer graph; the racks run the effects the tier already allows (`fxMask`). A bus that is silent and has no enabled effect is skipped.
* engine-own: none of the bits.


## WP6 - filters

Bits set: `kCapFilterExtras`, `kCapFilterModels`. Tests: `core/engine-vital/tests/test_wp6.cpp` (7 cases).

* **var / pan / level** for `filter1`, `filter2`, `fxfilter` (modulation destinations): `level` ramps the output after the mix (-60 dB = silence, 0 dB leaves the signal untouched), `pan` offsets the cutoff of the left and right channel by +-12 semitones in opposite directions (`StereoCutoffOffset`; applies to every model, with pan 0 the eight old models are bit-identical), `var` is the fourth knob of the new models (no effect on the eight old ones, tested). The bus sends of WP5 tap the filter after mix, pan and level.
* **Models 8 .. 21 (37 types)** through `core/filters-serum` (`SerumFilterBank`, one `filters::Filter` per lane): Multi SVF (12), Slope (6), EQ 6 dB (3), Ring Mod (2), Sample Hold (2), Smear (3), French LP, Add Bass, Scream (2), New Wsp, DJ Mixer, Diffusor, EMS Ladder, PZ SVF. Style values beyond a model's own count clamp to its last (tested). Names stay provisional (Q13). All 37 render finite, bounded (< 4.0) and audibly different from the Analog model, styles of one model differ from each other (at a `var` away from the neutral 50 %: the EQ gain and Ring Mod spread are neutral there by design); random cutoff / resonance / drive / var / pan / level in all three filter slots are stable.
* **Mapping of the Vital controls** (cutoff note -> Hz per sample, resonance 0..1, drive dB / 20, blend / 2, `filterN.mix` stays the module's own dry / wet). The package smooths cutoff and resonance with a ~30 Hz one pole smoother (its design): a fast envelope or audio rate modulation of the cutoff is smoothed on these models; the eight old models are unchanged.
* **The other filter types of core/filters-serum** (the 80 types of the Serum manual: MG Low 12/24, Low/High/Band/Peak/Notch 12/24, the flangers, formants, ...) map to the eight existing models of the table (04 section 1.3, A18) and are not appended: spec section 3 freezes the 14 appended models / 37 types.
* **PZ SVF curve data**: `content.filter_curves` -> `FilterCurveShared::fit ()` (message thread: one bell per point, Q from the spacing, a fixed point iteration on the gains so that the cascade follows the points) -> the voice filters run the bells instead of the package's PZ SVF; the cutoff shifts the curve along the frequency axis (1 kHz = as drawn). Measured on noise: drawn +36 dB between 200 Hz and 2 kHz, rendered 36.2 dB; +12 dB between 632 Hz and 2 kHz, rendered 12.5 dB. **Without curve data the PZ SVF type runs the package's pole / zero SVF** (not the "flat" of 13 section 5, which was written for a missing reference; a flat filter would make the type unusable).
* **Tiers**: no model is bypassed on low / medium; 4 scalar filters per voice pair per slot (memory about 180 KB per filter at 96 kHz, about 25 MB for the 136 filters of the voice slots).
* engine-own: none of the bits.

## WP7 - new FX DSP

Bits set: `kCapFxUtility`, `kCapFxHyperDimension`, `kCapFxBode`, `kCapFxConvolve`, `kCapFxSplitters`. Tests: `core/engine-vital/tests/test_wp7.cpp` (9 cases), random settings in every rack (`serum_modules_in_all_racks_survive_random_settings`) and the random key runs of `test_rt_alloc`.

* **Utility** (`core/fx-serum-a`): polarity L / R, low / high pass (the extreme position bypasses the filter, A21), mono bass, width (200 % doubles the side signal, 0 % is mono: side / off < 1 %), pan (100 % = right 3x louder), mix, level (-12 dB measured exactly). All at the neutral position the signal passes (difference < 2 %).
* **Hyper / Dimension** (`core/fx-serum-a`): changes the sound by > 30 % at mix 100 %, spreads the voices over the stereo field, Dimension alone widens, level. `hyperdim.retrig` restarts the hyper voices on every note on (`SoundEngine::noteOn` -> `noteTriggered ()`).
* **Bode** (`core/fx-serum-b`): a 440 Hz sine with range 100 Hz, shift 100 %: 540 Hz up, 340 Hz down, 440 Hz with shift 0 (spectrum peak, tolerance 5 Hz). Mapping: shift % x range = the shift in Hz, dir % = direction, feedback 0 .. 100 % -> 0 .. 0.95, delay in ms or synced (division window of `kDivisions` 4 .. 12; Tempo / dotted / triplet). The package's Hilbert filter has a latency of 64 samples (reported by the package, not compensated: a few ms on the wet signal only).
* **Convolve** (`core/fx-serum-b`, impulse from the content store): `convolve.ir` = Preset IR reads `content.impulses[0]` (WAV, mono or stereo) through the `IContentStore`; tone, pre-delay (ms or synced), mix and level act at once; **size, minimum phase, attack, decay, damping and IR gain re-partition the impulse (allocation) on a builder thread** (poll every 10 ms) and the audio thread takes the finished convolver over at the start of a block: a change is audible after the build (tens of milliseconds for a short impulse, longer for a long one; the old convolver keeps running meanwhile). Until the first build the module passes the signal. Size 50 % = the original length, 0 / 100 % = x0.25 / x4 (tested: the click of the impulse moves from 150 ms to 600 ms). The built-in impulse list is empty (`None`, `Preset IR`), as in the spec (A23).
  * `test_rt_alloc` counts allocations of the thread that calls `process ()` only now (`alloc_hook.h`): the builder thread is allowed to allocate. The audio thread allocates nothing (all random-key runs).
* **Splitters L/H, L/M/H, M/S** (`core/fx-serum-a` crossovers, LR4): the band racks host the effects (WP5 `runRack`), `<fx>.rack` = a band rack is inert while its splitter is off or not in the rack of the signal (tested), summing all bands with no effect is transparent: **-0.004 dB (L/H), -0.039 dB (L/M/H), 0.000 dB (M/S)** against the 0.1 dB of A24. **A bypassed band is removed from the sum** (the semantics of the package and of the wording of A24; Serum's own meaning of the button may differ). The package does not report latency for the crossovers (LR4 all-pass compensation inside).
* **Tiers**: `Quality::fxMask` has bits 9 .. 15 for the new modules (`quality.h`); **tier low bypasses Hyper / Dimension, Bode and Convolve** (plus reverb and flanger as before), medium and high run everything. Tested: Hyper masked on low, running on high; Utility runs on both.
* engine-own: none of the bits (the iOS engine has no chain of 16 modules).

## WP8 - reverb force enable

Bit set: `kCapReverbForce`. `effectiveFxMask (tierMask, reverbForceEnable)` in `quality.h` ORs `kFxReverb` into the tier's mask (only the reverb, not the flanger); the adapter applies it to every `Cap::fx` binding and re-applies `reverb.on` when the key changes. Tested on tier low: `reverb.on` masked (identical to no reverb, tail 0), with `reverb.force_enable` the tail is more than 20x the masked tail, switching it off removes it again, and the helper keeps the flanger masked.

## Gates and verification

The gate runs are in the section "Gates" at the end of this file.

## BLOCKED / not done

Nothing of the five work packages is left unset in the capability bits. Not done, with the exact reason:

1. (done in FIX1, see FIX1-REPORT.md) `oscN.type` crossfade.
2. **Granular time stretch for `scan`** (pitch independent speed): the scan is varispeed. Reason: Q6 keeps granular GAP.
3. **Vital import to `oscN.type = Sample`, the `.sbwt` format, the Kick pages and the MPC page cycler**: WP9 / C4, not part of this list.
4. Bode latency compensation: done in FIX1. The DAW-visible tail of Convolve is still not reported to the host.
5. `loop_link`: editor action by specification (A39), not a BLOCKED item.
## Gates (run once at the end of the whole list, on the final tree)

* `cmake -S . -B build -DSYNTHYBOY_BUILD_PLUGIN=ON && cmake --build build -j8`: exit 0, VST3 and CLAP built; the only compiler warnings are the 14 `-Wmissing-field-initializers` of the older tests (`test_wp123.cpp`, `test_rt_alloc.cpp`, from WP2); none in the new code.
* `ctest --test-dir build -j6`: **33 / 33 passed** (including `synthyboy_test_vital_audio`, the slow audio gates, and the golden checks `version_2_sound_is_unchanged_in_both_engines`: the version 2 default and busy patch hash unchanged; `synthyboy_test_rt`: 0 allocations; the new `synthyboy_test_vital_wp4` .. `wp7`).
* `build/clap_smoke build/SynthyBoy_artefacts/Release/CLAP/SynthyBoy.clap`: all checks passed. `scripts/check-params-doc.sh build/tools/paramdump/synthyboy_paramdump`: `docs/params.json` and `docs/PARAMETERS.md` are up to date (no key was added or changed).
* AddressSanitizer + UndefinedBehaviorSanitizer (separate build directory `build-asan`, RelWithDebInfo, `-fsanitize=address,undefined`): `synthyboy_test_vital_wp4` (8 cases), `wp5` (9), `wp6` (7), `wp7` (9), `synthyboy_test_rt` (4) and `synthyboy_test_sampleosc` (12): all passed, no sanitizer report. The ASan run found one bug in the test of the sample package (`sampleosc_loop_continuity_no_click` rendered 8064 frames into an 8000 frame buffer): fixed in the test.
* clap-validator and ThreadSanitizer were not run (clap-validator is not installed on this machine; the only new thread is the Convolve builder, which communicates through atomics).
